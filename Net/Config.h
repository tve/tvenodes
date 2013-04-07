// Copyright (c) 2013 Thorsten von Eicken
//
// EEPROM Configuration class: supports the configuration of numerous code modules via EEPROM

#ifndef CONFIG_H
#define CONFIG_H

// Code module IDs
#define NET_MODULE	    0
#define LOG_MODULE	    1
#define NETTIME_MODULE  2
#define OWTEMP_MODULE   3

class Configured {
public:
  virtual uint8_t moduleId(void) = 0;           // return the module id
  virtual uint8_t configSize(void) = 0;         // return the size of the eeprom config block
	virtual void applyConfig(uint8_t *) = 0;			// apply the config that was read from EEPROM
	virtual void receive(volatile uint8_t *pkt, uint8_t len) = 0;  // process a received packet
};

extern void config_init(Configured **modules);
extern void config_write(uint8_t moduleId, uint8_t *data);
extern bool config_read(uint8_t moduleId, uint8_t *data);

// Dispatch a received packet to the appropriate Configured's receive() method.
// This is typically called after net.poll, e.g.: "if (net.poll()) config_dispatch();"
extern void config_dispatch(void);

#endif // CONFIG_H
