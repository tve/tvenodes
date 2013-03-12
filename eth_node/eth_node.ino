// Copyright (c) 2013 by Thorsten von Eicken
// Ethernet Relay Node
//   Ether Card
// Functions:
//   - queries NTP server for current time and broadcasts on rf12 net

#include <JeeLib.h>
#include <EtherCard.h>

static byte mymac[] = { 0x74,0x69,0x69,0x2D,0x30,0x31 };
static byte ntpServer[] = { 192, 168, 0, 3 };
static uint32_t time;

//===== setup & loop =====

void setup() {
  initLCD();
  Serial.begin(57600);
  Serial.print("\n***** RUNNING: ");
  Serial.println(__FILE__);

	Serial.print("MAC: ");
	for (byte i = 0; i < 6; ++i) {
		Serial.print(mymac[i], HEX);
		if (i < 5) Serial.print(':');
	}
	Serial.println();
														  
	if (ether.begin(sizeof Ethernet::buffer, mymac) == 0) 
		Serial.println( "Failed to access Ethernet controller");

	Serial.println("Setting up DHCP");
	if (!ether.dhcpSetup())
		Serial.println( "DHCP failed");
										
	ether.printIp("My IP: ", ether.myip);
	ether.printIp("Netmask: ", ether.mymask);
	ether.printIp("GW IP: ", ether.gwip);
	ether.printIp("DNS IP: ", ether.dnsip);

  ether.ntpRequest(ntpServer, 100);
  while (!ether.ntpProcessAnswer(&time, 100)
    ;
  Serial.print("The time is: ");
  Serial.println(time);


}

void loop() {
}
