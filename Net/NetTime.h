// Copyright (c) 2013 Thorsten von Eicken
//
// Time class, receives time messages and keeps local time up-to-date

#ifndef NETTIME_H
#define NETTIME_H

// Assumes JeeLib.h is included for rf12 constants

class NetTime : public Configured {
  // Configuration structure stored in EEPROM
  typedef struct {
    int8_t	offset;	  // time zone offset
  } nettime_config;

  int8_t offset;

public:
	// constructor
	NetTime(void);

  // Configuration methods
	virtual void applyConfig(uint8_t *);
	virtual void receive(volatile uint8_t *pkt, uint8_t len);
};

#endif // NETTIME_H
