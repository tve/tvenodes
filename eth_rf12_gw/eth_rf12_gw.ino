// Copyright (c) 2013 by Thorsten von Eicken
// Ethernet Relay Node
//   Ether Card
// Functions:
//   - relay packets pretty much as-is between rf12 and eth
//   - queries NTP server for current time and broadcasts on rf12 net
// LEDs:
//   - red LED on while IP address is 0.0.0.0     (D to gnd LED_RED_PORT)
//   - green LED toggles when eth pkt received    (D to gnd LED_RCV_PORT)
//   - yellow LED toggles when rf12 pkt received  (A to gnd LED_RCV_PORT)

#include <EtherCard.h>
#include <JeeLib.h>
#include <avr/eeprom.h>

//===== USER CONFIGURATION =====

#define LED_RED_PORT	          4
#define LED_RCV_PORT	          3

#define RF12_ID                31       // this node's ID
#define RF12_BAND     RF12_915MHZ
#define RF12_GROUP           0xD4				// 0xD4 is JeeLabs' default group
#define RF12_LOWPOWER           1				// use low power TX in the lab
#define RF12_19KBPS             0				// use slow data rate for long range
#define RF12_RSSI 						  1				// 0:none, 1=analog RSSI, 2=digital RSSI

// my IP configuration
static uint8_t my_ip[] = { 192, 168, 0, 24 };   // my IP address, statically assigned
//static uint8_t gw_ip[] = { 192, 168, 0, 3 };		// gateway IP address, statically assigned
static byte mymac[] = { 0x74,0x69,0x69,0x2D,0x30,0x32 };

// NTP server
#define NTP 0										 				// whether to do NTP or not
#if NTP
static byte ntpServer[] = { 192, 168, 0, 3 };
static word ntpPort = 123;              // port on which NTP responds
#endif

// Hub server: this is where we send messages to and expect messages from
static byte hubServer[] = { 192, 168, 0, 3 };
static word hubPort = 9999;             // port on which the hub service runs

//===== end of user configuration =====

// Ethernet data
byte Ethernet::buffer[500];						// tcp/ip send and receive buffer
#define gPB ether.buffer

// Timers and ports
static MilliTimer ntpTimer;           // timer for sending ntp requests
static MilliTimer chkTimer;           // timer for checking with server
static MilliTimer grnTimer, ylwTimer; // timer to blink green & yellow LEDs
static Port redLed(LED_RED_PORT);     // red:D&gnd
static Port rcvLed(LED_RCV_PORT);     // grn:D&gnd ylw:A&gnd

static uint32_t num_rf12_rcv = 0;     // counter of rf12 packets received
static uint32_t num_rf12_snd = 0;     // counter of rf12 packets sent
static uint32_t num_eth_rcv = 0;      // counter of ethernet packets received
static uint32_t num_eth_snd = 0;      // counter of ethernet packets sent

// RSSI data for all the nodes
#define RF12_NUMID 32                 // number of nodes
#if RF12_RSSI
static uint8_t rcvRssi[RF12_NUMID-2]; // RSSI measured by ourselves
static uint8_t ackRssi[RF12_NUMID-2]; // RSSI received from remote node in ACK packets
#endif

// the time...
#if NTP
static uint32_t time, frac;
#endif

//===== Ethernet logging =====
// Simple class that will send text in ethernet messages to the hub server.

class LogEth : public Print {
private:
  uint8_t buffer[133];
  uint8_t ix;

  virtual void ethSend(uint8_t *buffer, uint8_t len) {
    //Serial.print("LogEth::ethSend(");
    //Serial.print(len);
    //Serial.println(")");
    ether.udpPrepare(hubPort, hubServer, hubPort);
    uint8_t *ptr = gPB+UDP_DATA_P;

    // Need to construct fake rf12 packet
    *ptr++ = 0xD4;  // group
    *ptr++ = RF12_ID;
    *ptr++ = len;
    memcpy(ptr, buffer, len);
    ether.udpTransmit(len+3);
    num_eth_snd++;
  }

	void send(void) {
		buffer[ix] = 0;
		// print to serial
		Serial.print((char *)buffer);
		if (ix > 0 && buffer[ix-1] == '\n')
			Serial.print('\r');
		// log to ethernet
		ethSend(buffer, ix);

		ix = 0;
	}

public:
	// write a character to the buffer, used by Print but can also be called explicitly
	// automatically sends the buffer when it's full or a \n is written
	size_t write (uint8_t v) {
		buffer[ix++] = v;
		if (ix >= sizeof(buffer)-1) {
			send();
		} else if (v == 012) {
		  send();
		}
		return 1;
	}
};

LogEth logger;

//===== ntp response with fractional seconds

#if NTP
byte ntpProcessAnswer(uint32_t *time, uint32_t *frac, byte dstport_l) {
  if ((dstport_l && gPB[UDP_DST_PORT_L_P] != dstport_l) || gPB[UDP_LEN_H_P] != 0 ||
      gPB[UDP_LEN_L_P] != 56 || gPB[UDP_SRC_PORT_L_P] != 0x7b)
    return 0;
  ((byte*) time)[3] = gPB[UDP_DATA_P + 40]; // 0x52];
  ((byte*) time)[2] = gPB[UDP_DATA_P + 41];
  ((byte*) time)[1] = gPB[UDP_DATA_P + 42];
  ((byte*) time)[0] = gPB[UDP_DATA_P + 43];
  ((byte*) frac)[3] = gPB[UDP_DATA_P + 44];
  ((byte*) frac)[2] = gPB[UDP_DATA_P + 45];
  ((byte*) frac)[1] = gPB[UDP_DATA_P + 46];
  ((byte*) frac)[0] = gPB[UDP_DATA_P + 47];
  return 1;
}
#endif

//===== message from management server

byte msgProcessAnswer() {
	// If the length of the UDP packet is too short or too long fuhgetit
  uint8_t len = gPB[UDP_LEN_L_P] - UDP_HEADER_LEN;
  if (gPB[UDP_DST_PORT_L_P] != (hubPort & 0xff) ||
      gPB[UDP_DST_PORT_H_P] != (hubPort >> 8) ||
      gPB[UDP_LEN_H_P] != 0 ||
      len < 3 || len > 3+RF12_MAXDATA)
    return 0;

	// Make sure the length of the UDP packet matches the rf12_len field in the payload
  if (len != gPB[UDP_DATA_P+2]+3) {
    logger.print(F("ETH: length mismatch udp:"));
    logger.print(len);
    logger.print(" ");
    logger.print(gPB[UDP_DATA_P+2]);
    logger.println("+3");
    return 1;
  }

#if 0
  // Print UDP packet -- WARNING: it corrupts the incoming packet!!!
  logger.print("UDP:");
  for (uint8_t i=0; i<len+UDP_HEADER_LEN; i++) {
    logger.print(" ");
    logger.print(gPB[UDP_SRC_PORT_H_P+i], HEX);
  }
  logger.println();
#endif

	// Send the packet on RF. It would be nice to be able to handle some incoming packets,
	// but we'd need some buffering for that...
	uint8_t hdr = gPB[UDP_DATA_P+1];
	rf12_sendNow(hdr, gPB+UDP_DATA_P+3, len-3);

#if 1
  logger.print(F("ETH  RCV packet: hdr=0x"));
  logger.print(hdr, HEX);
  logger.print(F(" len="));
  logger.print(len-3);
  logger.println();
#endif

  return 1;
}

//===== RF12 helpers =====

// Switch rf12 to low power transmission and low gain rcv. Must be called *after*
// rf12_initialize. This is very useful when having nodes sit a few inches apart when testing.
// At full power the xmit can overdrive the receiver resulting in poor/no reception.
void rf12_lowpower(void) {
  rf12_control(0x9857); // !mp,90kHz,MIN OUT
  rf12_control(0x94B2); // VDI,FAST,134kHz,-14dBm rcv,-91dBm rssi
}

// Switch rf12 to 19kbps for longer range. 19kbps seems to be a sweet spot in terms of
// throughput vs. range. There's a big range jump from 38.4k to 19.2k and a smaller one
// down to 9.6k. Also, that starts to become really slow. See p.37 of the si4421 datasheet.
// This must be called after rf12_initialize and it conflicts with rf12_lowpower
void rf12_19kbps(void) {
	rf12_control(0xC611); // 19.1 kbps
	rf12_control(0x94C1); // VDI:fast,-97dBm,67khz
	rf12_control(0x9820); // 45khz
}

#if RF12_RSSI == 1
// Measure the RSSI on analog pin SCL / A5 / pin-19, assumes a wire from the RF12 to that pin
// bypassed with a 1nF capacitor. The measurement must be done as soon as possible after
// reception. This should really be integrated into the rf12 library...
#define RSSI_PIN 5 // A5
void rf12_initRssi() {
  analogReference(INTERNAL);
  //pinMode(RSSI_PIN, INPUT); // A5 is digital pin 19, ugh...
}
uint8_t rf12_getRssi() {
	return (uint16_t)(analogRead(RSSI_PIN)-300) >> 2;
}
#elif RF12_RSSI == 2
/* digital RSSI */
#else
void rf12_initRssi() { }
uint8_t rf12_getRssi() { return 0; }
#endif

//===== dump memory =====
#if 0
void dumpMem(void) {
  for (intptr_t a=0x0100; a<0x1000; a++) {
    if ((a & 0xf) == 0) {
      Serial.print("0x");
      Serial.print(a, HEX);
      Serial.print("  ");
    }
    Serial.print((*(uint8_t*)a)>>4, HEX);
    Serial.print((*(uint8_t*)a)&0xF, HEX);
    Serial.print(" ");
    if ((a & 0xf) == 0xF) Serial.println();
  }
}
#endif

//===== setup =====

void setup() {
  Serial.begin(57600);
  Serial.println(F("***** SETUP: " __FILE__));

	// LED check for 500ms
  rcvLed.mode2(OUTPUT);    // yellow
  rcvLed.digiWrite2(1);    // ON
  rcvLed.mode(OUTPUT);
  rcvLed.digiWrite(1);     // green ON
  redLed.mode(OUTPUT);
  redLed.digiWrite(1);     // red ON
	delay(500);

	rf12_initialize(RF12_ID, RF12_BAND, RF12_GROUP);
#if RF12_LOWPOWER
	Serial.println(F("RF12: low TX power"));
	rf12_lowpower();
#endif
#if RF12_19KBPS
	Serial.println(F("RF12: 19kbps"));
	rf12_19kbps();
#endif
  
	// Print MAC address for debugging
  Serial.print("MAC: ");
  for (byte i = 0; i < 6; ++i) {
		Serial.print(mymac[i], HEX);
		if (i < 5) Serial.print(':');
  }
  Serial.println();

	// Print error if the EtherCard isn't well connected
  while (ether.begin(sizeof Ethernet::buffer, mymac) == 0) {
    Serial.println(F("Failed to access Ethernet controller"));
    delay(1000);
  }
  ether.staticSetup(my_ip, hubServer, hubServer);
  
  // Get timers going
  ntpTimer.poll(1100);
  chkTimer.poll(1000);

  Serial.println(F("***** RUNNING: " __FILE__));
  rcvLed.digiWrite2(0);       // turn yellow off
  rcvLed.digiWrite(0);        // turn green off
}

//===== loop =====

void loop() {

  bool ethReady = ether.isLinkUp() && !ether.clientWaitingGw();

  // Every few seconds send an NTP time request
#if NTP
  if (ethReady && ntpTimer.poll(5000)) {
    ether.ntpRequest(ntpServer, ntpPort);
    Serial.println("Sending NTP request");
  }
#endif

  // Debug printout if we're not connected
  if (!ethReady && ntpTimer.poll(4000)) {
    Serial.print("ETH link:");
    Serial.print(ether.isLinkUp() ? "UP" : "DOWN");
    Serial.print(" GW:");
    Serial.print(ether.clientWaitingGw() ? "WAIT" : "OK");
    Serial.println();
  }

  // Once a minute send an announcement packet and include the RSSIs of all the nodes
  //if (ethReady && chkTimer.poll(59900)) {
  if (ethReady && chkTimer.poll(4900)) {
    ether.udpPrepare(hubPort, hubServer, hubPort);
#define MSG "\xD4\x1\x0\x0"
    uint16_t sz = sizeof(MSG)-1;
    memcpy(gPB + UDP_DATA_P, MSG, sz);
		gPB[UDP_DATA_P+3+(sz++)] = num_rf12_rcv;
		gPB[UDP_DATA_P+3+(sz++)] = num_rf12_snd;
		gPB[UDP_DATA_P+3+(sz++)] = num_eth_rcv;
		gPB[UDP_DATA_P+3+(sz++)] = num_eth_snd;
#if RF12_RSSI
    memcpy(gPB + UDP_DATA_P + sz, rcvRssi, sizeof(rcvRssi)); sz += sizeof(rcvRssi);
    memcpy(gPB + UDP_DATA_P + sz, ackRssi, sizeof(ackRssi)); sz += sizeof(ackRssi);
    memset(rcvRssi, 0, sizeof(rcvRssi));
    memset(ackRssi, 0, sizeof(ackRssi));
#endif
		gPB[UDP_DATA_P+2] = sz-3; // fake rf12_len
    ether.udpTransmit(sz);
    num_eth_snd++;
		// Print our IP address as a sign of being alive & well
    ether.printIp(F("IP: "), ether.myip);
		Serial.print("RF12: rcv="); Serial.print(num_rf12_rcv);
		Serial.print(" snd=");      Serial.print(num_rf12_snd); 
		Serial.print(" ETH: rcv="); Serial.print(num_eth_rcv);
		Serial.print(" snd=");      Serial.print(num_eth_snd);
		Serial.println();
  }

  // Receive RF12 packets.
	// rf12_recvDone returns true if it received a broadcast packet (D=0)
	// -or- D=1 and the dest is us -or- D=1 and the dest is node 31
  if (rf12_recvDone()) {
    num_rf12_rcv++;

#if RF12_RSSI
    // Record RSSIs
    uint8_t node = rf12_hdr & RF12_HDR_MASK;
		if ((rf12_hdr & RF12_HDR_DST) == 0) { // if the pkt has the source address
			rcvRssi[node] = rf12_getRssi();
		}
		if ((rf12_hdr & ~RF12_HDR_MASK) == RF12_HDR_CTL && // ACK pkt with source addr
			  rf12_len == 1)                                 // and with one data byte
		{
      ackRssi[node-2] = rf12_data[0];
		}
#endif

#if 1
    Serial.print("RCV hdr=");
    Serial.print(rf12_hdr & RF12_HDR_MASK);
    Serial.print(" rssi=");
    Serial.println(rcvRssi[node]);

    logger.print(F("RF12 RCV packet: hdr=0x"));
    logger.print(rf12_hdr, HEX);
    logger.print(F(" len="));
    logger.print(rf12_len);
		logger.println();
#endif
    
    // Forward packets to management server, for now we just forward everything making this
		// a completely transparent gateway
		//logger.println(F(" to mgmnt server"));
		ether.sendUdp((char *)rf12_buf, rf12_len+3, hubPort, hubServer, hubPort);
		num_eth_snd++;

    // Turn yellow LED on for 100ms
    ylwTimer.set(100);
    rcvLed.digiWrite2(1); // yellow on
  }
  
  // Receive ethernet packets
  int plen = ether.packetReceive();
  ether.packetLoop(plen);

  if (plen > 42 &&       // minimum UDP packet length
      gPB[ETH_TYPE_H_P] == ETHTYPE_IP_H_V &&
      gPB[ETH_TYPE_L_P] == ETHTYPE_IP_L_V &&
      gPB[IP_PROTO_P] == IP_PROTO_UDP_V)
  {
    num_eth_rcv++;

    // Handle DHCP packets
#   define DHCP_SRC_PORT 67
    if (gPB[UDP_SRC_PORT_L_P] == DHCP_SRC_PORT) {
#if 0
			// I've had trouble with DHCP interactions with ARP, static IPs are easier...
      EtherCard::DhcpStateMachine(plen);
#endif

    // Handle management server packets
    } else if (msgProcessAnswer()) {
      // Turn green LED on for 100ms
      grnTimer.set(100);
      rcvLed.digiWrite(1); // green on

#if NTP
    // Check for NTP responses and forward time
    } else if (ntpProcessAnswer(&time, &frac, ntpPort)) {
      // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
      const unsigned long seventyYears = 2208988800UL;     
      // subtract seventy years and set local clock
      time = time - seventyYears;
      setTime(time);

      // Try to send it once on the rf12 radio (don't let it get stale)
			// We could try and adjust for the ethernet+rf12 delay, but too much trouble...
      if (rf12_canSend()) {
        struct net_time { uint8_t module; uint32_t time; } tbuf = { NETTIME_MODULE, time };
        rf12_sendStart(RF12_ID, &tbuf, sizeof(tbuf));
        num_rf12_snd++;
        //logger.println(F("Sent time update"));
      } else {
        //logger.println(F("Cannot send time update"));
      }

      // print the time
      time_t t = now();
      logger.print(month(t));  logger.print('/');
      logger.print(day(t));    logger.print('/');
      logger.print(year(t));   logger.print(' ');
      logger.print(hour(t));   logger.print(':');
      logger.print(minute(t)); logger.print(':');
      logger.print(second(t)); logger.print(" = ");
      logger.print(time);
      //logger.print('.');       logger.print(frac);
      logger.println();
#endif
    }
  }

  // Update LEDs
  if (ylwTimer.poll()) {
    rcvLed.digiWrite2(0);                 // turn yellow off
  }
	if (grnTimer.poll()) {
		rcvLed.digiWrite(0);                  // turn green off
	}
	redLed.digiWrite(ethReady ? 0 : 1);     // ethernet redy LED
  
}
