// Copyright (c) 2013 by Thorsten von Eicken
// Seedling misting and lighting node
//   Relay plug
// Functions:
//   - 

#define USE_LCD
#define HELLO ("\n***** RUNNING: " __FILE__)

#include <JeeLib.h>
#include <Time.h>
#include <Net.h>
#include <OwTemp.h>

#define RLY_PORT      1
#define LCD_PORT      2
#define OW_PORT       3
#define N_TEMP        1

#ifdef USE_LCD
# include <PortsLCD.h>
  PortI2C myI2C (2);
  LiquidCrystalI2C lcd (myI2C);
# define screen_width 16
# define screen_height 2
#endif

Console console;
MilliTimer notSet, lcdUpdate;
uint8_t lastRssi = 0;

//===== Temperature Sensors =====

// Polls and keeps track of a number of one-wire temperature sensors. Reads the sensors
// every few seconds and keeps track of daily min/max by having an array of the min/max
// for every hour and shifting that. So at any point in time it has the min/max for the
// past 24 hours with a 1-hour granularity.

OwTemp owt(OW_PORT+3);
byte temp_num = 0;          // number of temp sensors

#define TEMP_PERIOD 20      // how frequently to read sensors (in seconds)
#define MAX_TEMP 2          // max number of temperature sensors supported
#define T_AIR    0

// Sensor 1-wire addresses
uint64_t temp_addr[MAX_TEMP] = {
  0x9c000001319DB628LL, // air -- blue wire
  0x0000000000000028LL, // spare
  //0x0c00000131945B28LL, // test sensor
};

// Temperature names
char temp_name[MAX_TEMP][5] = { "Air ", "?" };

// Temperatures
float  temp_now[MAX_TEMP];      // current temperatures
int8_t temp_min[MAX_TEMP][24];  // per-hour minimum temperatures for past 24 hours
int8_t temp_max[MAX_TEMP][24];  // per-hour maximum temperatures for past 24 hours

// Timer/counter to shift min/max temps every hour
MilliTimer minMaxTimer;  // 60 second timer
byte minMaxCount = 0;    // count 60 minutes

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

// Update temperature sensors and keep track on min/max
// Keeps it own timer to know when to read temp sensors, force=true overrides
// that and forces a read
void loop_temp(boolean force=false) {
  if (owt.poll(force ? 0 : 20)) {
    console.print("Temperatures: ");
    for (int s=0; s<N_TEMP; s++) {
      console.print(temp_name[s]); console.print(":");
      float t = owt.get(temp_addr[s]);
      if (isnan(t)) t = owt.get(temp_addr[s]);
      if (!isnan(t)) {
        temp_now[s] = t;
        int8_t rt = (int8_t)(temp_now[s] + 0.5);
        if  (rt < temp_min[s][0]) temp_min[s][0] = rt;
        if  (rt > temp_max[s][0]) temp_max[s][0] = rt;
        console.print(temp_now[s]); console.print(" ");
      } else {
        console.print("NaN   ");
      }
    }
    console.println();

    // rotate min/max temp every hour
    if (minMaxTimer.poll(60000)) {
      minMaxCount++;
      if (minMaxCount == 60) {
        minMaxCount = 0;
        // rotate min/max temps
        for (int s=0; s<N_TEMP; s++) {
          for (int i=23; i>0; i--) {
            temp_min[s][i] = temp_min[s][i-1];
            temp_max[s][i] = temp_max[s][i-1];
          }
        }
      }
    }
  }
}

// Initialize temperature sensor "module"
void setup_temp() {
  find_temp();
  loop_temp(true);

  // init min/max arrays
  for (int i=0; i<MAX_TEMP; i++) {
    for (int j=0; j<24; j++) {
      temp_min[i][j] = (int8_t)(temp_now[i]+0.5);
      temp_max[i][j] = (int8_t)(temp_now[i]+0.5);
    }
  }
}

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
  if (isnan(temp_now[T_AIR])) return;
  if (timeStatus() != timeSet) return;

  time_t t = now() % (time_t)(onTime+offTime);
  bool on = t < (uint32_t)onTime;

  if (!onNow && on) {
    // off->on transition only if the temperature is high enough
    on = temp_now[T_AIR] > onTemp;
  }

  onDly = on ? (time_t)onTime - t : (time_t)(onTime+offTime) - t;

  if (on != onNow) {
    relay.digiWrite(on);
    relay.mode(OUTPUT);
    console.print("Turning misting ");
    console.println(on ? "on" : "off");
    onNow = on;
  }
}


//===== setup & loop =====

void setup() {
  Serial.begin(57600);
  Serial.println(HELLO);

# ifdef USE_LCD
  lcd.begin(screen_width, screen_height);
  lcd.print("=>" __FILE__);
# endif
  
  net_setup(NET_SEED_NODE, true);
  console.println(HELLO);

  setup_misting();
  setup_temp();
  notSet.set(1000);
}

void loop() {
  net_packet *pkt;
  time_t t;
  bool wasSet;

  // Keep the network movin'
  if ((pkt = net_poll())) {
    lastRssi = rf12_getRSSI();
    //Serial.print("RCV rssi=");
    //Serial.println(lastRssi);
    switch (pkt->hdr.type) {
    case net_time:
      wasSet = timeStatus();
      // set local clock
      setTime(pkt->time.time);
      if (!wasSet) console.println("Time initialized");

      // print the time
      //t = now();
      //console.print("Got updated time ");
      //console.println(t);
      break;
    default:
      console.print("Unknown message type=");
      console.println(pkt->hdr.type);
      break;
    }
  }

  // If we don't know the time of day, just sit there and wait for it to be set
  if (timeStatus() != timeSet) {
    if (notSet.poll(60000)) {
      console.println("Time not set");
      lcd.setCursor(0,1); // second line
      lcd.print("Time not set");
    }
    return;
  }

  // Main functionality
  loop_temp();
  loop_misting();

  // Update the time display on the LCD
#ifdef USE_LCD
  if (lcdUpdate.poll(1000)) {
    char buf[17];

    lcd.clear();
    lcd.print("T="); lcd.print((int)temp_now[T_AIR]); lcd.print(" ");

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
