// Copyright (c) 2013 Thorsten von Eicken
//
// EEPROM Configuration class: supports the configuration of numerous code modules via EEPROM

#include <JeeLib.h>
#include <util/crc16.h>
#include <avr/eeprom.h>
#include <Config.h>

#define EEPROM_ADDR (0x20)
#define EEPROM_MAX  (64)			// max size of a config block

static Configured  **configs = 0;			// list of modules, each implementing Configured
static uint8_t     config_cnt = 0;		// number of modules
static uint16_t    config_sz = 0;			// total size of configs in eeprom

void config_init(Configured **cf) {
	// count the number of configs
	config_cnt = 0;
	while (cf[config_cnt] != 0) config_cnt++;
	configs = cf;
  Serial.print("Config: ");
  Serial.print(config_cnt);
  Serial.println(" configs");

	// iterate through modules to calculate total size in EEPROM
	config_sz = 0;
	for (uint8_t i=0; i<config_cnt; i++) {
		uint8_t sz = cf[i]->configSize();
		if (sz > EEPROM_MAX) {
			Serial.print("CONFIG: the size of the config for the ");
      Serial.print(i+1); Serial.print("th module is too large (");
			Serial.print(sz); Serial.print(" vs. ");
      Serial.print(EEPROM_MAX); Serial.println(" max)");
			continue;
		}
		config_sz += sz;
	}
	config_sz += 2; // CRC
  Serial.print("  size: ");
  Serial.println(config_sz);

	// check CRC
	uint8_t *eeprom_addr = (uint8_t *)EEPROM_ADDR;
	uint16_t crc = ~0;
	for (uint16_t i=0; i<config_sz; i++)
		crc = _crc16_update(crc, eeprom_read_byte(eeprom_addr + i));
	if (crc != 0) {
		// give each module's applyConfig a rain-check
    Serial.println("  CRC does not match!");
		for (uint8_t i=0; i<config_cnt; i++)
			cf[i]->applyConfig(0);
		return;
	}
  Serial.println("  CRC matches!");

	// read each config and pass to applyConfig
	uint8_t config_block[EEPROM_MAX];
	eeprom_addr = (uint8_t *)EEPROM_ADDR;
	for (uint8_t i=0; i<config_cnt; i++) {
		// read from eeprom
		eeprom_read_block(config_block, eeprom_addr, cf[i]->configSize());
		// call applyConfig
		cf[i]->applyConfig(config_block);
	}
}

void config_dispatch(void) {
  if (rf12_len < 1) return;
  uint8_t module = rf12_data[0];

	// iterate through modules and dispatch to correct one
	for (uint8_t i=0; i<config_cnt; i++) {
		uint8_t m = configs[i]->moduleId();
		if (m == module) {
      configs[i]->receive(rf12_data+1, rf12_len-1);
      return;
		}
	}
}
