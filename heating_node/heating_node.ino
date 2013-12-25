// Copyright (c) 2013 by Thorsten von Eicken
//
// Controller for sub-floor radint heating system: monitors temperature sensors and 
// controls zone circulation pumps. All this via 1-wire buses.


#include <NetAll.h>
#include <OwTemp.h>
#include <OwRelay.h>
#include <avr/eeprom.h>

#define OWT_PORT       1    // Port for temperature sensors
#define OWR_PORT       2    // Port for relay switches

#define MAX_TEMP      16
#define TEMP_PERIOD   10    // how frequently to read sensors (in seconds)
#define MAX_RELAY      5
#define RELAY_PERIOD 120    // how frequently to read relays (in seconds)

Net net(0xD4, false);  // default group_id and normal power
NetTime nettime;
Log l, *logger=&l;
MilliTimer notSet, toggle, debugTimer;
OwTemp owTemp(OWT_PORT+3, MAX_TEMP);
OwRelay owRelay(OWR_PORT+3, MAX_RELAY);
bool relayOut[MAX_RELAY];

// Temperature names
char temp_name[MAX_TEMP][5] = { "Air ", "?" };

//===== setup & loop =====

static Configured *(node_config[]) = {
  &net, logger, &nettime, &owTemp, &owRelay, 0
};

void setup() {
  Serial.begin(57600);
  Serial.println(F("***** SETUP: " __FILE__));

  //eeprom_write_word((uint16_t *)0x20, 0xF00D);

  config_init(node_config);

  owTemp.setup((Print*)logger);
  owRelay.setup((Print*)logger);
  owRelay.swap(0, 1);
  logger->println(F("***** RUNNING: " __FILE__));
}

int cnt = 0;
void loop() {
  if (net.poll()) {
    config_dispatch();
  }

  owTemp.loop(TEMP_PERIOD);
  owRelay.loop(RELAY_PERIOD);

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

  // If we don't know the time of day complain about it
  if (timeStatus() != timeSet) {
    if (notSet.poll(60000)) {
      logger->println("Time not set");
    }
  }

  // Toggle both relays
  if (0 && toggle.poll(1000)) {
    cnt += 1;
    owRelay.loop(0);
    relayOut[0] = cnt & 1;
    logger->print("Relay 0 is ");
    logger->print(owRelay.get(0));
    logger->print(" setting to ");
    logger->print(relayOut[0]);
    if (owRelay.set(0, relayOut[0])) {
      logger->println(" OK");
    } else {
      logger->println(" FAILED");
    }

    relayOut[1] = (cnt>>1)&1;
    logger->print("Relay 1 is ");
    logger->print(owRelay.get(1));
    logger->print(" setting to ");
    logger->print(relayOut[1]);
    if (owRelay.set(1, relayOut[1])) {
      logger->println(" OK");
    } else {
      logger->println(" FAILED");
    }
  }

}
