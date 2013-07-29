// Copyright (c) 2013 by Thorsten von Eicken
//
// Simple test node with optional One-Wire DS18B20 sensor on port 1

#include <NetAll.h>
#include <OwTemp.h>
#include <avr/eeprom.h>

#define OW_PORT       1

#define MAX_TEMP     2
#define TEMP_PERIOD 10      // how frequently to read sensors (in seconds)

Net net(0xD4, true);  // default group_id and low power
NetTime nettime;
Log l, *logger=&l;
MilliTimer notSet;
MilliTimer debugTimer;
OwTemp owTemp(OW_PORT+3, MAX_TEMP);

// Temperature names
char temp_name[MAX_TEMP][5] = { "Air ", "?" };

//===== setup & loop =====

static Configured *(node_config[]) = {
  &net, logger, &nettime, &owTemp, 0
};

void setup() {
  Serial.begin(57600);
  Serial.println(F("***** SETUP: " __FILE__));

  //eeprom_write_word((uint16_t *)0x20, 0xF00D); // reset EEPROM

  config_init(node_config);

  owTemp.setup((Print*)logger);
  notSet.set(1000);
  logger->println(F("***** RUNNING: " __FILE__));
}

void loop() {
  if (net.poll()) {
    config_dispatch();
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
    }
    return;
  }

}
