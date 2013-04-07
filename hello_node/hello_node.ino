// Copyright (c) 2013 by Thorsten von Eicken
//
// Simple test node with One-Wire DS18B20 sensor on port 1 and BlinkPlug on port 2
// Optionally LCD for local display of info

#undef USE_LCD
#define HELLO ("***** RUNNING: " __FILE__ "\n")

#include <JeeLib.h>
#include <Time.h>
#include <Config.h>
#include <Net.h>
#include <Log.h>
#include <NetTime.h>
#include <OwTemp.h>

#define OW_PORT       1
#define BLINK_PORT    2
#define LCD_PORT      3
#define N_TEMP        1

#ifdef USE_LCD
# include <PortsLCD.h>
  PortI2C myI2C (2);
  LiquidCrystalI2C lcd (myI2C);
# define screen_width 16
# define screen_height 2
#endif

#define MAX_TEMP     2
#define TEMP_PERIOD 20      // how frequently to read sensors (in seconds)

Net net(0xD4, true);  // default group_id and low power
NetTime nettime;
Log logger;
MilliTimer notSet, lcdUpdate;
MilliTimer debugTimer;
OwTemp owTemp(OW_PORT+3, MAX_TEMP);

// Temperature names
char temp_name[MAX_TEMP][5] = { "Air ", "?" };

//===== setup & loop =====

static Configured *(node_config[]) = {
  &net, &logger, &nettime, &owTemp, 0
};

void setup() {
  Serial.begin(57600);
  Serial.println(HELLO);

  config_init(node_config);

# ifdef USE_LCD
  lcd.begin(screen_width, screen_height);
  lcd.print("=>" __FILE__);
# endif
  
  logger.println(HELLO);

  owTemp.setup((Print*)&logger);
  notSet.set(1000);
}

void loop() {
  if (net.poll())
    config_dispatch();
  owTemp.loop(TEMP_PERIOD);

  // Debug to serial port
  if (debugTimer.poll(10000)) {
    Serial.println("OwTimer debug...");
    owTemp.printDebug((Print*)&logger);
  }

  // If we don't know the time of day, just sit there and wait for it to be set
  if (timeStatus() != timeSet) {
    if (notSet.poll(60000)) {
      logger.println("Time not set");
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
