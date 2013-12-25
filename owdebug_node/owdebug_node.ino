// Copyright (c) 2013 by Thorsten von Eicken
//
// Simple test node with One-Wire DS18B20 sensor on port 1 and BlinkPlug on port 2
// Optionally use LCD on port 3 for local display of info

#include <NetAll.h>
#include <OwScan.h>
#include <OwTemp2.h>
#include <OwMisc.h>
#include <avr/eeprom.h>

#define OW_PORT       2

#define MAX_TEMP      2
#define MAX_DEV       2
#define TEMP_PERIOD  10      // how frequently to read sensors (in seconds)

Net net(0xD4, true);  // default group_id and low power
NetTime nettime;
Log l, *logger=&l;
MilliTimer scanTimer;
MilliTimer debugTimer;

OwScan owScan(OW_PORT+3, MAX_DEV);
OwTemp owTemp(&owScan, MAX_TEMP);
OwMisc owMisc(&owScan);

//===== setup & loop =====

static Configured *(node_config[]) = {
  &net, logger, &nettime, &owScan, 0
};

void setup() {
  Serial.begin(57600);
  Serial.println(F("***** SETUP: " __FILE__));

  eeprom_write_word((uint16_t *)0x20, 0xF00D);

  config_init(node_config);

  scanTimer.set(1000);

  logger->println(F("***** RUNNING: " __FILE__));
}

void loop() {
  if (net.poll()) {
    config_dispatch();
  }

	if (scanTimer.poll(10000)) {
    owScan.scan((Print*)logger);
    owScan.printDebug((Print*)logger);

    // counters
#define CNT 1
		uint32_t n = owMisc.ds2423GetCount(CNT, 0);
		logger->print("Countr @");
		owScan.printAddr(logger, owScan.getAddr(CNT));
		logger->print(" A->");
		if (n == ~0) logger->print(" BADCRC"); else logger->print(n);
		logger->println();

		n = owMisc.ds2423GetCount(CNT, 1);
		logger->print("Countr @");
		owScan.printAddr(logger, owScan.getAddr(CNT));
		logger->print(" B->");
		if (n == ~0) logger->print(" BADCRC"); else logger->print(n);
		logger->println();

		n = owMisc.ds2423GetCount(CNT, 2);
		logger->print("Countr @");
		owScan.printAddr(logger, owScan.getAddr(CNT));
		logger->print(" C->");
		if (n == ~0) logger->print(" BADCRC"); else logger->print(n);
		logger->println();
	}

}
