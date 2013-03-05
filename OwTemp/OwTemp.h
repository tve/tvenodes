// C 2013 Thorsten von Eicken

#ifndef OwTemp_h
#define OwTemp_h

#define ONEWIRE_CRC8_TABLE 1
#include <OneWire.h>

// number of temp sensors
#define OWTEMP_COUNT 8

#define INT16_MIN ((int16_t)0x8000)

class OwTemp {
public:
  OwTemp (byte pin);

  byte setup(byte num, uint64_t *addr); // init with given addrs, returns number of sensors found
  byte poll(uint8_t secs);          // polls temp every secs, returns true if conversion ready
  float get(byte i);                // get temperature from nth sensor
  float get(uint64_t addr);         // get temperature from sensor by address
  uint64_t temp_addr[OWTEMP_COUNT]; // sensor addresses

protected:
  OneWire ds;
  // Temperature sensor
  byte temp_count;
  byte temp_state; // 0=off, 1=idle, 2=converting
  unsigned long temp_last;
  float temp[OWTEMP_COUNT];
  //byte temp_addr[OWTEMP_COUNT][8];

	void setresolution(uint64_t addr, byte bits);
	void start();
	int16_t rawRead(uint64_t addr);
	float read(uint64_t addr);
	void print(uint64_t addr);
};

#endif
