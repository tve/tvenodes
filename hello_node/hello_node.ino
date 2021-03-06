// Copyright (c) 2013 by Thorsten von Eicken
//
// Simple test node with One-Wire DS18B20 sensor on port 1 and BlinkPlug on port 2
// Optionally use LCD on port 3 for local display of info

#undef USE_LCD

#include <NetAll.h>
#include <OwTemp.h>
#include <avr/eeprom.h>

#define OW_PORT       1
#define BLINK_PORT    2
#define LCD_PORT      3

#ifdef USE_LCD
# include <PortsLCD.h>
  PortI2C myI2C (2);
  LiquidCrystalI2C lcd (myI2C);
# define screen_width 16
# define screen_height 2
#endif

#define MAX_TEMP     2
#define TEMP_PERIOD 10      // how frequently to read sensors (in seconds)

Net net(0xD4, true);  // default group_id and low power
NetTime nettime;
Log l, *logger=&l;
MilliTimer notSet, lcdUpdate;
MilliTimer debugTimer;
OwTemp owTemp(OW_PORT+3, MAX_TEMP);
BlinkPlug blk(2);

// Temperature names
char temp_name[MAX_TEMP][5] = { "Air ", "?" };

//===== setup & loop =====

static Configured *(node_config[]) = {
  &net, logger, &nettime, &owTemp, 0
};

void setup() {
  Serial.begin(57600);
  Serial.println(F("***** SETUP: " __FILE__));
  blk.ledOn(3);

  //eeprom_write_word((uint16_t *)0x20, 0xF00D);

  config_init(node_config);

# ifdef USE_LCD
  lcd.begin(screen_width, screen_height);
  lcd.print("=>" __FILE__);
# endif
  
  owTemp.setup((Print*)logger);
  notSet.set(1000);
  logger->println(F("***** RUNNING: " __FILE__));
  blk.ledOff(3);
}

void loop() {
  if (net.poll()) {
    blk.ledOn(1);
    config_dispatch();
    delay(10);
    blk.ledOff(1);
  }

  owTemp.loop(TEMP_PERIOD);

  // Debug to serial port
  if (debugTimer.poll(7770)) {
    //logger->println("OwTimer debug...");
    //owTemp.printDebug((Print*)logger);
    logger->print("Temp: ");
    logger->print(owTemp.get(0));
    logger->print("F [");
    logger->print(owTemp.getMin(0));
    logger->print("..");
    logger->print(owTemp.getMax(0));
    logger->println("]");
  }

  // If we don't know the time of day, just sit there and wait for it to be set
  if (timeStatus() != timeSet) {
    if (notSet.poll(60000)) {
      logger->println("Time not set");
#ifdef USE_LCD
      lcd.setCursor(0,1); // second line
      lcd.print("Time not set");
#endif
    }
    return;
  }

  // Update the time display on the LCD
#ifdef USE_LCD
  if (lcdUpdate.poll(1000)) {
    char buf[17];
    time_t t;

    lcd.clear();
    lcd.print("T="); lcd.print((int)temp_now[T_AIR]); lcd.print(" ");

    t = now();
    char div = second(t) & 1 ? ':' : '\xA5';
    snprintf(buf, 17, "\xB7%02d  %2d/%2d %2d%c%02d",
        lastRssi, month(t), day(t), hour(t), div, minute(t));
    //Serial.println(buf);
    lcd.setCursor(16-strlen(buf),1); // second line
    lcd.print(buf);
  }
#endif
  
}
