// Copyright (c) 2013 Thorsten von Eicken
//
// Network Time class

#include <JeeLib.h>
#include <Config.h>
#include <Net.h>
#include <Time.h>
#include <Log.h>
#include <NetTime.h>

// constructor
NetTime::NetTime(void) {
	offset = 0; // UTC default
  moduleId = NETTIME_MODULE;
  configSize = sizeof(nettime_config);
}

// ===== Configuration =====

// Receive a time packet with UTC time
void NetTime::receive(volatile uint8_t *pkt, uint8_t len) {
  if (len >= 4) {
    bool wasSet = timeStatus();
    setTime(*(uint32_t *)pkt);
    if (!wasSet) logger.println("Time initialized");
  }
}

void NetTime::applyConfig(uint8_t *cf) {
  if (cf)
    offset = *(int8_t *)cf;
  else
    config_write(NETTIME_MODULE, &offset);
}
