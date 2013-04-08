// Copyright (c) 2013 Thorsten von Eicken
//
// Logging class, the output can be directed to the serial port, an LCD display,
// and/or the network.

#ifndef LOG_H
#define LOG_H

#include <Config.h>

// Assumes JeeLib.h is included for rf12 constants
#define LOG_MAX (RF12_MAXDATA-5)		// max amount of chars that can be logged in one packet

class Log : public Print, public Configured {
  // Configuration structure stored in EEPROM
  typedef struct {
    bool	serial:1;	// log to serial port
    bool	lcd:1;		// log to LCD
    bool	net:1;		// log to the network
    bool	time:1;		// log the time with each packet
  } log_config;

  log_config config;
  uint8_t buffer[LOG_MAX];
  uint8_t ix;

	// send accumulated buffer
  void send(void);

public:
	// constructor
	Log(void);

	// write a character to the buffer, used by Print but can also be called explicitly
	// automatically prints/sends the buffer when it's full or a \n is written
	virtual size_t write (uint8_t v);

  // Configuration methods
	virtual void applyConfig(uint8_t *);
	virtual void receive(volatile uint8_t *pkt, uint8_t len);
};

extern Log logger;

#endif // LOG_H
