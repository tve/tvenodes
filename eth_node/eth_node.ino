// Copyright (c) 2013 by Thorsten von Eicken
// Ethernet Relay Node
//   Ether Card
// Functions:
//   - queries NTP server for current time and broadcasts on rf12 net
//   - relay packets pretty much as-is between rf12 and eth
// BlinkPlug port 2:
//   - red LED on when IP address is 0.0.0.0      (D to gnd)
//   - green LED toggles when eth pkt received    (D to vcc)
//   - yellow LED toggles when rf12 pkt received  (A to vcc)

#include <EtherCard.h>
#include <NetAll.h>
#include <avr/eeprom.h>

// Ethernet data
byte Ethernet::buffer[500];   // tcp/ip send and receive buffer
#define gPB ether.buffer
static byte mymac[] = { 0x74,0x69,0x69,0x2D,0x30,0x32 };
static byte ntpServer[] = { 192, 168, 0, 3 };
static word ntpClientPort = 123;
static byte msgServer[] = { 192, 168, 0, 3 };
static word msgClientPort = 9999;
static char msgSelf[] = "\xD4\x01\x03\x01\x0D\xF0"; // announcement packet

// Timers and ports
Net net(0xD4, false);                 // default group_id and normal power
static MilliTimer ntpTimer;           // timer for sending ntp requests
static MilliTimer chkTimer;           // timer for checking with server
static MilliTimer grnTimer, ylwTimer; // timer to blink green & yellow LEDs
static Port led(2);                   // LEDs: D:green/red, A:yellow/off

static uint32_t num_rf12_rcv = 0;     // counter of rf12 packets received
static uint32_t num_rf12_snd = 0;     // counter of rf12 packets sent
static uint32_t num_eth_rcv = 0;      // counter of ethernet packets received
static uint32_t num_eth_snd = 0;      // counter of ethernet packets sent

// the time...
static uint32_t time, frac;

//===== Ethernet logging =====

class LogEth : public Log {
protected:
  virtual void ethSend(uint8_t *buffer, uint8_t len) {
    //Serial.print("LogEth::ethSend(");
    //Serial.print(len);
    //Serial.println(")");
    ether.udpPrepare(msgClientPort, msgServer, msgClientPort);
    uint8_t *ptr = gPB+UDP_DATA_P;

    // Need to construct fake rf12 packet
    *ptr++ = 0xD4;  // group
    *ptr++ = node_id;
    *ptr++ = len;
    *ptr++ = LOG_MODULE;
    memcpy(ptr, buffer, len);
    ether.udpTransmit(len+4);
    num_eth_snd++;
  }
};

LogEth loggerEth;
Log *logger = &loggerEth;

// modules
static Configured *(node_config[]) = {
  &net, &loggerEth, 0
};

//===== ntp response with fractional seconds

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

//===== message from management server

byte msgProcessAnswer() {
  uint8_t len = gPB[UDP_LEN_L_P] - UDP_HEADER_LEN;
  if (gPB[UDP_DST_PORT_L_P] != (msgClientPort & 0xff) ||
      gPB[UDP_DST_PORT_H_P] != (msgClientPort >> 8) ||
      gPB[UDP_LEN_H_P] != 0 ||
      len < 3 || len > 3+RF12_MAXDATA)
    return 0;

  if (len != gPB[UDP_DATA_P+2]+3) {
    logger->print(F("ETH: length mismatch udp:"));
    logger->print(len);
    logger->print(" ");
    logger->print(gPB[UDP_DATA_P+2]);
    logger->println("+3");
    return 1;
  }

  if (gPB[UDP_DATA_P+1] == (NET_GW_NODE|RF12_HDR_DST)) {
    //logger->println("Packet to self");
    config_dispatch(gPB+UDP_DATA_P+3, len-3);
    return 1;
  }

#if 0
  // Print UDP packet -- WARNING: it corrupts the incoming packet!!!
  logger->print("UDP:");
  for (uint8_t i=0; i<len+UDP_HEADER_LEN; i++) {
    logger->print(" ");
    logger->print(gPB[UDP_SRC_PORT_H_P+i], HEX);
  }
  logger->println();
#endif

  uint8_t *pkt = net.alloc();
  if (pkt) {
    uint8_t to=gPB[UDP_DATA_P+1], d=gPB[UDP_DATA_P+3];
    memcpy(pkt, gPB+UDP_DATA_P+3, len-3);
    net.rawSend(len-3, gPB[UDP_DATA_P+1]);
    num_rf12_snd++;
#if 0
    logger->print(F("ETH  RCV packet: hdr=0x"));
    logger->print(to, HEX);
    logger->print(F(" module="));
    logger->print(d);
    logger->print(F(" len="));
    logger->print(len-3);
    logger->println();
#endif
  }

  return 1;
}

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
  led.mode2(OUTPUT);    // yellow
  led.digiWrite2(0);    // ON
  led.mode(OUTPUT);
  led.digiWrite(0);     // red

  // Uncomment to reset EEPROM config
  //eeprom_write_word((uint16_t *)0x20, 0xDEAD);

  config_init(node_config);
  if (node_id != NET_GW_NODE) {
    net.setNodeId(NET_GW_NODE);
  }
  node_enabled = true;
  
#if 1
  Serial.print("MAC: ");
  for (byte i = 0; i < 6; ++i) {
          Serial.print(mymac[i], HEX);
          if (i < 5) Serial.print(':');
  }
  Serial.println();
#endif

  while (ether.begin(sizeof Ethernet::buffer, mymac) == 0) {
    Serial.println(F("Failed to access Ethernet controller"));
    delay(1000);
  }
  uint8_t my_ip[] = { 192, 168, 0, 64 };
  uint8_t gw_ip[] = { 192, 168, 0, 3 };
  ether.staticSetup(my_ip, gw_ip, gw_ip);
  
  // Get timers going
  ntpTimer.poll(1100);
  chkTimer.poll(1000);

  Serial.println(F("***** RUNNING: " __FILE__));
  led.mode2(INPUT);       // turn yellow off
}
 //===== loop =====

void loop() {

  bool ethReady = ether.isLinkUp() && !ether.clientWaitingGw();

  // Every few seconds send an NTP time request
  if (ethReady && ntpTimer.poll(5000)) {
    ether.ntpRequest(ntpServer, ntpClientPort);
    //Serial.println("Sending NTP request");
  }

  // Debug printout if we're not connected
  if (!ethReady && ntpTimer.poll(1000)) {
    Serial.print("ETH link:");
    Serial.print(ether.isLinkUp() ? "UP" : "DOWN");
    Serial.print(" GW:");
    Serial.print(ether.clientWaitingGw() ? "WAIT" : "OK");
    Serial.println();
  }

  // Once a minute send an announcement packet
  if (ethReady && chkTimer.poll(59900)) {
    ether.sendUdp(msgSelf, sizeof(msgSelf)-1, msgClientPort, msgServer, msgClientPort);
    num_eth_snd++;
    ether.printIp(F("IP: "), ether.myip);
  }

  // Keep rf12 moving
  if (net.poll()) {
    /*
    int lastRssi = rf12_getRSSI();
    Serial.print("RCV hdr=");
    Serial.print(rf12_hdr & RF12_HDR_MASK);
    Serial.print(" rssi=");
    Serial.println(lastRssi);
    */

    /*
    logger->print(F("RF12 RCV packet: hdr=0x"));
    logger->print(rf12_hdr, HEX);
    logger->print(F(" module="));
    logger->print(*(uint8_t*)rf12_data);
    logger->print(F(" len="));
    logger->print(rf12_len);
    */
    num_rf12_rcv++;
    
    // Forward packets to management server
    if ((rf12_hdr & RF12_HDR_DST) == 0) {
      //logger->println(F(" to mgmnt server"));
      ether.sendUdp((char *)rf12_buf, rf12_len+3, msgClientPort, msgServer, msgClientPort);
      num_eth_snd++;
    } else {
      //logger->println();
    }

    // Turn yellow LED on for 100ms
    ylwTimer.set(100);
    led.mode2(OUTPUT); // yellow on
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
      EtherCard::DhcpStateMachine(plen);
#endif

    // Handle management server packets
    } else if (msgProcessAnswer()) {
      // Turn green LED on for 100ms
      grnTimer.set(100);
      led.mode(OUTPUT);
      led.digiWrite(1); // green on

    // Check for NTP responses and forward time
    } else if (ntpProcessAnswer(&time, &frac, ntpClientPort)) {
      // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
      const unsigned long seventyYears = 2208988800UL;     
      // subtract seventy years and set local clock
      time = time - seventyYears;
      setTime(time);

      // Try to send it once on the rf12 radio (don't let it get stale)
      if (rf12_canSend()) {
        struct net_time { uint8_t module; uint32_t time; } tbuf = { NETTIME_MODULE, time };
        rf12_sendStart(NET_GW_NODE, &tbuf, sizeof(tbuf));
        num_rf12_snd++;
        //logger->println(F("Sent time update"));
      } else {
        //logger->println(F("Cannot send time update"));
      }

      // print the time
      time_t t = now();
      logger->print(month(t));
      logger->print('/');
      logger->print(day(t));
      logger->print('/');
      logger->print(year(t));
      logger->print(' ');
      logger->print(hour(t));
      logger->print(':');
      logger->print(minute(t));
      logger->print(':');
      logger->print(second(t));
      logger->print(" = ");
      logger->print(time);
      //logger->print('.');
      //logger->print(frac);
      logger->println();
    }
  }

  // Update LEDs
  if (ylwTimer.poll()) {
    led.mode2(INPUT);                 // turn yellow off
  }
  if (ethReady) {
    if (grnTimer.poll()) {
      led.mode(INPUT);                // turn green off
    }
  } else {
    led.mode(OUTPUT);
    led.digiWrite(0);                 // ethernet not ready: keep red on
  }
  
}
