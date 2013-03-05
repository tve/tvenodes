#include <OwTemp.h>
#include <OneWire.h> // needed by makefile, ugh

#define OWTEMP_CONVTIME 200 //188 // milliseconds for a conversion

OwTemp::OwTemp(byte pin) : ds(pin)
{
  temp_count = 0;
  temp_state = 0;
}

void OwTemp::print(uint64_t addr) {
  uint8_t *a = (uint8_t *)&addr;
  Serial.print("0x");
  for (byte b=0; b<8; b++) {
    Serial.print(*a >> 4, HEX);
    Serial.print(*a & 0xF, HEX);
    a++;
  }
}

byte OwTemp::setup(byte num, uint64_t *addrs) {
  // add what we're told to our address table
  temp_count = num;
  for(byte s=0; s<num; s++) temp_addr[s] = addrs[s];

  // run a search on the bus to see what we actually find
  uint64_t addr;                   // next detected sensor
  uint16_t found = 0;              // which addrs we actually found
  byte n_found = 0;                // number of sensors actually discovered
	ds.reset_search();
  while (ds.search((uint8_t *)&addr)) {
    // make sure the CRC is valid
		byte crc = OneWire::crc8((uint8_t *)&addr, 7);
    if (crc != (addr>>56)) continue;

    n_found++;

    // see whether we know this sensor already
    for (byte s=0; s<temp_count; s++) {
      if (addr == temp_addr[s]) {
        // yup! we know this one
        if (s < num) found |= (uint16_t)s << s;  // mark sensor as found
        goto cont;
      }
    }

    // new sensor, if we have space add it
    if (temp_count < OWTEMP_COUNT) {
      temp_addr[temp_count++] = addr;
    }

  cont: ;
  }
	ds.reset_search();

  // make sure all temp sensors are set to 10 bits of resolution
  for (byte s=0; s<temp_count; s++)
    if ((temp_addr[s]&0xff) == 0x22 || (temp_addr[s]&0xff) == 0x28)
      setresolution(temp_addr[s], 10);

  // print info about additional sensors found
  if (temp_count > num) {
    Serial.print("Unknown sensors:");
    for (byte s=num; s<temp_count; s++) {
      Serial.print(" ");
      print(temp_addr[s]);
    }
    Serial.println();
  }

  // print info about missing sensors
  if (found != (1<<num)-1) {
     Serial.print("Missing sensors:");
     //Serial.print(" ("); Serial.print(found, HEX); Serial.print(") ");
     for (byte s=0; s<num; s++) {
       if (!(found & (1<<s))) {
         Serial.print(" ");
         print(temp_addr[s]);
       }
    }
    Serial.println();
  }

  // start a conversion
  if (temp_count > 0) {
		temp_last = millis();
		temp_state = 2;
		start();
    delay(OWTEMP_CONVTIME);
  }
  return n_found;
}

// Poll temperature sensors every <secs> seconds; use secs=0 to force conversion now
byte OwTemp::poll(byte secs) {
  unsigned long now = millis();
  switch (temp_state) {
    case 0: // no sensors
      break;
    case 1: // idle
      if (secs == 0 || now - temp_last > (unsigned long)secs * 1000) {
        // start a conversion
        temp_last = now;
        temp_state = 2;
        start();
      }
      break;
    case 2: // conversion in progress
      if (now - temp_last > OWTEMP_CONVTIME) {
        for (int i=0; i<temp_count; i++) {
          temp[i] = read(temp_addr[i]);
        }
        temp_state = 1;
				return true;
      }
  }
	return false;
}

float OwTemp::get(uint64_t addr) {
  for (byte i=0; i<temp_count; i++) {
    if (temp_addr[i] == addr) return temp[i];
  }
  return NAN;
}

// Set the resolution
void OwTemp::setresolution(uint64_t addr, byte bits) {
  // write scratchpad
  ds.reset();
  ds.select((uint8_t *)&addr);
  ds.write(0x4E, 0);
  ds.write(0, 0);                    // temp high
  ds.write(0, 0);                    // temp low
  ds.write(((bits-9)<<5) + 0x1F, 0); // configuration
  // copy to EEPROM
  ds.reset();
  ds.select((uint8_t *)&addr);
  ds.write(0x48, 1);                  // copy to eeprom with strong pullup
  delay(10);                          // needs 10ms
  ds.depower();
}

// Start temperature conversion
void OwTemp::start() {
  ds.reset();
  ds.skip();
  ds.write(0x44, 1);         // start conversion, with parasite power on at the end
}

// Raw reading of temperature, returns INT16_MIN on failure
int16_t OwTemp::rawRead(uint64_t addr) {
  byte data[12];
  ds.reset();
  ds.select((uint8_t *)&addr);    
  ds.write(0xBE);         // Read Scratchpad
  
  for (byte i = 0; i < 9; i++) {           // we need 9 bytes
    data[i] = ds.read();
  }
  if (OneWire::crc8(data, 8) != data[8]) {
#if DEBUG
    Serial.print("OWT: Bad CRC   for ");
    print(addr);
    Serial.print("->");
    print(*(uint64_t *)data);
    Serial.println();
#endif
    return INT16_MIN;
  }

  // handle missing sensor and double-check data
  if ((data[0] == 0 && data[1] == 0) || data[5] != 0xFF || data[7] != 0x10) {
#if DEBUG
    Serial.print("OWT: Bad data for ");
    print(addr);
    Serial.print("->");
    print(*(uint64_t *)data);
    Serial.println();
#endif
    return INT16_MIN;
  }

#if DEBUG
  Serial.print("OWT: Good data for ");
  print(addr);
  Serial.print("->");
  print(*(uint64_t *)data);
  Serial.println();
#endif
  
  // mask out bits according to precision of conversion
  int16_t raw = ((uint16_t)data[1] << 8) | data[0];
  int16_t t_mask[4] = {0x7, 0x3, 0x1, 0x0};
  byte cfg = (data[4] & 0x60) >> 5;
  raw &= ~t_mask[cfg];
  return raw;
}

// Read the temperature
float OwTemp::read(uint64_t addr) {
  int16_t raw = rawRead(addr);
  if (raw == INT16_MIN) raw = rawRead(addr);
  if (raw == INT16_MIN) raw = rawRead(addr);
  if (raw == INT16_MIN) raw = rawRead(addr);
  if (raw == INT16_MIN) return NAN;

  float celsius = (float)raw / 16.0;
  float fahrenheit = celsius * 1.8 + 32.0;
  
  return fahrenheit;
}
