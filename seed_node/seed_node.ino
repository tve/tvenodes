// Copyright (c) 2013 by Thorsten von Eicken
// Seedling misting and lighting node
//   Relay plug
// Functions:
//   - 

#define USE_LCD

#include <NetAll.h>
#include <OwTemp.h>
#include <avr/eeprom.h>

#define RLY_PORT      1
#define LCD_PORT      2
#define OW_PORT       3
#define MAX_TEMP      2
#define TEMP_PERIOD  20      // how frequently to read sensors (in seconds)

#ifdef USE_LCD
# include <PortsLCD.h>
  PortI2C myI2C (2);
  LiquidCrystalI2C lcd (myI2C);
# define screen_width 16
# define screen_height 2
#endif

Net net(0xD4, true);  // default group_id and low power
NetTime nettime;
Log l, *logger=&l;
OwTemp owTemp(OW_PORT+3, MAX_TEMP);
MilliTimer notSet, lcdUpdate;
uint8_t lastRssi = 0;

char temp_name[MAX_TEMP][5] = { "Air ", "?" };

#if 0
// Find temperature sensors and print what we found
void find_temp() {
  temp_num = owt.setup(N_TEMP, temp_addr);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Found ");
  lcd.print(temp_num);
  lcd.print("/");
  lcd.print(N_TEMP);
  lcd.println(" temps ");

  Serial.print("Found ");
  Serial.print(temp_num);
  Serial.print(" of ");
  Serial.print(N_TEMP);
  Serial.println(" sensors");
}
#endif

//===== misting timer =====

uint16_t onTime = 10;         // seconds misting is on
uint16_t offTime = 10*60;     // seconds misting is off
uint8_t  onTemp = 70;         // above which temp to turn misting on (in F)
bool     onNow = false;       // whether misting is on now
time_t   onDly = 0;           // delay until switching

Port relay(RLY_PORT);

void setup_misting() {
    relay.digiWrite(0);
    relay.mode(OUTPUT);
    relay.digiWrite2(0);
    relay.mode2(OUTPUT);
}

void loop_misting() {
  float temp = owTemp.get(0);
  if (isnan(temp)) return;
  if (timeStatus() != timeSet) return;

  time_t t = now() % (time_t)(onTime+offTime);
  bool on = t < (uint32_t)onTime;

  if (!onNow && on) {
    // off->on transition only if the temperature is high enough
    on = temp > onTemp;
  }

  onDly = on ? (time_t)onTime - t : (time_t)(onTime+offTime) - t;

  if (on != onNow) {
    relay.digiWrite(on);
    relay.mode(OUTPUT);
    logger->print("Turning misting ");
    logger->println(on ? "on" : "off");
    onNow = on;
  }
}

//===== setup & loop =====

static Configured *(node_config[]) = {
  &net, logger, &nettime, &owTemp, 0
};

void setup() {
  Serial.begin(57600);
  Serial.println(F("***** SETUP: " __FILE__));

  //eeprom_write_word((uint16_t *)0x20, 0xF00D); // reset EEPROM content

  config_init(node_config);

# ifdef USE_LCD
  lcd.begin(screen_width, screen_height);
  lcd.print("=>" __FILE__);
# endif
  
  owTemp.setup((Print*)logger);
  setup_misting();
  notSet.set(1000);
  logger->println(F("***** RUNNING: " __FILE__));
}

void loop() {
  time_t t;

  // Keep the network movin'
  if (net.poll()) {
    lastRssi = rf12_getRSSI();
    config_dispatch();
    Serial.print("RCV rssi=");
    Serial.println(lastRssi);
  }

  owTemp.loop(TEMP_PERIOD);

  // If we don't know the time of day, just sit there and wait for it to be set
  if (timeStatus() != timeSet) {
    if (notSet.poll(60000)) {
      logger->println("Time not set");
      lcd.setCursor(0,1); // second line
      lcd.print("Time not set");
    }
    return;
  }

  // Main functionality
  loop_misting();

  // Update the time display on the LCD
#ifdef USE_LCD
  if (lcdUpdate.poll(1000)) {
    char buf[17];

    lcd.clear();
    lcd.print("T="); lcd.print((int)owTemp.get(0)); lcd.print(" ");

    if (onNow) {
      lcd.print("ON  ");
    } else {
      lcd.print("OFF ");
    }
    lcd.print(onDly);

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
