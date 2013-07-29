// Copyright (c) 2013 by Thorsten von Eicken
//
// Node to test the RF12B network displaying RSSI values on an LCD display
// Sends short packets to the central gateway node and expects ACKs back that contain the
// RSSI received at the gateway node.
// Connect LCD on P2

#undef  USE_LCD
#define USE_GLCD

#include <NetAll.h>
#include <avr/eeprom.h>

#define BLINK_PORT    1
#define LCD_PORT      2

#ifdef USE_LCD
# include <PortsLCD.h>
  PortI2C myI2C (LCD_PORT);
  LiquidCrystalI2C lcd (myI2C);
# define screen_width 20
# define screen_height 4
#endif
#ifdef USE_GLCD
# include <GLCD_ST7565.h>
# include "utility/font_clR6x8.h"
  GLCD_ST7565 glcd;
  extern byte gLCDBuf[1024]; // requires removing "static" in GLCD_ST7565.cpp
#endif

Net net(0xD4, false);  // default group_id and full power
NetTime nettime;
Log l, *logger=&l;
MilliTimer notSet, lcdUpdate, xmit;
MilliTimer debugTimer;

//===== setup & loop =====

static Configured *(node_config[]) = {
  &net, logger, &nettime, 0
};

void setup() {
  Serial.begin(57600);
  Serial.println(F("***** SETUP: " __FILE__));

  // uncomment to reset config info stored in EEPROM
  //eeprom_write_word((uint16_t *)0x20, 0xF00D);

  config_init(node_config);

# ifdef USE_LCD
  lcd.begin(screen_width, screen_height);
  lcd.print("=>" __FILE__);
# endif

# ifdef USE_GLCD
  glcd.begin(0x1a);
  glcd.clear();
  glcd.backLight(255);
  glcd.setFont(font_clR6x8);

  // draw a string at a location, use _p variant to reduce RAM use
  glcd.drawString_P(0,  0, PSTR(__FILE__));
  glcd.refresh();
# endif
  
  notSet.set(1000);
  //logger->println(F("***** RUNNING: " __FILE__));
}

uint8_t avgRcvRssi, avgAckRssi = 0;

void loop() {
  if (net.poll()) {
    config_dispatch();
  }

  // If we don't know the time of day, just sit there and wait for it to be set
  if (timeStatus() != timeSet) {
    if (notSet.poll(60000)) {
#ifdef USE_LCD
      lcd.clear();
      lcd.setCursor(0,0); // first line
      lcd.print("Time not set");
#endif
#ifdef USE_GLCD
      glcd.clear();
      glcd.drawString_P(0, 0, PSTR("Time not set"));
      glcd.refresh();
#endif
    }
    return;
  }

  if (xmit.poll(200)) {
    uint8_t *buf = net.alloc();
    if (buf) {
      net.send(0, true);
    }
  }

  // Update the time display on the LCD
# ifdef USE_LCD
  if (lcdUpdate.poll(1000)) {
    char buf[screen_width+1];
    time_t t;

    // First line has rcvRSSI and date/time
    t = now();
    char div = second(t) & 1 ? ':' : '\xA5';
    snprintf(buf, screen_width+1, "\xB7%03d  %2d/%2d %2d%c%02d",
        net.lastRcvRssi, month(t), day(t), hour(t), div, minute(t));
    //Serial.println(buf);
    lcd.setCursor(0,0); // first line
    lcd.print(buf);

    // Second line has rcv and snd RSSI
    lcd.setCursor(0,1); // second line
    snprintf(buf, screen_width+1, "rcv:%03d snd:%03d", net.lastRcvRssi, net.lastAckRssi);
    lcd.print(buf);
  }
# endif
# ifdef USE_GLCD
  if (lcdUpdate.poll(1000)) {
    char buf[20];
    time_t t;
    uint8_t rssi = net.lastRcvRssi;
    if (avgRcvRssi > 0)
      avgRcvRssi = avgRcvRssi - (avgRcvRssi>>3) + (rssi>>3); // exp decay average
    else
      avgRcvRssi = rssi;

    // clear the upper part of the LCD where the text goes
    glcd.fillRect(0, 0, LCDWIDTH, 16, 0);

    // First line has rcvRSSI and date/time
    t = now();
    char div = second(t) & 1 ? ':' : '-';
    snprintf(buf, 20, "%03d %2d/%2d %2d%c%02d",
        avgRcvRssi, month(t), day(t), hour(t), div, minute(t));
    //Serial.println(buf);
    glcd.drawString(0, 0, buf);

    // Second line has rcv and snd RSSI
    snprintf(buf, 20, "rcv:%03d snd:%03d", net.lastRcvRssi, net.lastAckRssi);
    glcd.drawString(0, 8, buf);
    net.lastRcvRssi = 0;
    net.lastAckRssi = 0;

    // Shift plot left one pixel and draw new RSSI as vertical line
    glcd.setUpdateArea(0,16,LCDWIDTH-1,LCDHEIGHT-1, false);
    for (byte l=2; l<8; l++) { // vertical blocks of 8 pix: do pix 16 thru 64
      byte *p = gLCDBuf + (l * 128);
      const byte x = 1; // shift by one pixel
      for (byte b = 0; b < LCDWIDTH-x; ++b)
        *(p+b) = *(p+b+x);
      for (byte b = LCDWIDTH-x; b < LCDWIDTH; ++b)
        *(p+b) = 0;
    }

    // Draw latest rcvRssi in right-most column
    if (rssi > 0)
      glcd.drawLine(LCDWIDTH-1, LCDHEIGHT-1-(rssi>>2), LCDWIDTH-1, LCDHEIGHT-1, 1);

    glcd.refresh();
  }
# endif

}
