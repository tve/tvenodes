// Copyright (c) 2013 Thorsten von Eicken
//
// Logging class

#include <JeeLib.h>
#include <Config.h>
#include <Net.h>
#include <Time.h>
#include <Log.h>

#define LCD		1  	// support logging to the LCD (set to 0 to exclude that code)
#define TIME	1		// support logging the time (set to 0 to exclude that code)

// constructor
Log::Log(void) {
	ix = 0;
	memset(&config, 0, sizeof(log_config));
	config.serial = true;
}

void Log::send(void) {
  buffer[ix] = 0;

	// Log to the serial port
	if (config.serial) {
		Serial.print((char *)buffer);
	}

	// Log to the network
	if (config.net) {
		uint8_t *pkt = net.alloc();
		if (pkt) {
			*pkt = LOG_MODULE;
#if TIME
			*(uint32_t *)(pkt+1) = now();
#else
			*(uint32_t *)(pkt+1) = 0;
#endif
			memcpy(pkt+5, buffer, ix);
			net.send(ix+5); // +5 for module_id byte and for time
		}
	}

	// Log to the LCD
#if LCD
	if (config.lcd) {
	}
#endif

	ix = 0;
}

// write a character to the buffer, used by Print but can also be called explicitly
// automatically sends the buffer when it's full or a \n is written
size_t Log::write (uint8_t v) {
	if (ix >= LOG_MAX) send();
	buffer[ix++] = v;
	if (v == 012) send();
	return 1;
}

// ===== Configuration =====

uint8_t Log::moduleId(void) { return LOG_MODULE; }
uint8_t Log::configSize(void) { return sizeof(log_config); }
void Log::receive(volatile uint8_t *pkt, uint8_t len) { return; } // this is never called :-)

void Log::applyConfig(uint8_t *cf) {
  if (cf) {
    memcpy(&config, cf, sizeof(log_config));
    Serial.print("Config Log: 0x");
    Serial.println(*cf, HEX);
  } else {
    memset(&config, 0, sizeof(log_config));
    config.serial = 1;
    Serial.println("Config Log: init to serial");
  }
}
