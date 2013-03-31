// Copyright (c) 2013 by Thorsten von Eicken
// Ethernet Relay Node
//   Ether Card
// Functions:
//   - queries NTP server for current time and broadcasts on rf12 net

#include <EtherCard.h>
#include <JeeLib.h>
#include <Time.h>
#include <Net.h>

#define HELLO ("\n***** RUNNING: " __FILE__)

// Ethernet data
byte Ethernet::buffer[550];   // tcp/ip send and receive buffer
static byte mymac[] = { 0x74,0x69,0x69,0x2D,0x30,0x32 };
static byte ntpServer[] = { 192, 168, 0, 3 };
static word ntpClientPort = 123;
static byte msgServer[] = { 192, 168, 0, 3 };
static word msgClientPort = 9999;

// the time...
static uint32_t time, frac;

//===== ntp response with fractional seconds
#define gPB ether.buffer

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

//===== ethernet UDP printer =====

void eth_print(uint8_t nodeId, time_t t, const char *txt) {
  ether.udpPrepare(msgClientPort, msgServer, msgClientPort);
  char *ptr = (char *)gPB+UDP_DATA_P;

  // print node ID
  *ptr++ = '@';
  *ptr++ = '0' + nodeId/10; *ptr++ = '0' + nodeId%10;
  *ptr++ = ' ';

  // print date/time
  *ptr++ = '0' + year(t)/1000%10; *ptr++ = '0' + year(t)/100%10;
  *ptr++ = '0' + year(t)/10%10;   *ptr++ = '0' + year(t)%10;   *ptr++ = '/';
  *ptr++ = '0' + month(t)/10;     *ptr++ = '0' + month(t)%10;  *ptr++ = '/';
  *ptr++ = '0' + day(t)/10;       *ptr++ = '0' + day(t)%10;    *ptr++ = ' ';
  *ptr++ = '0' + hour(t)/10;      *ptr++ = '0' + hour(t)%10;   *ptr++ = ':';
  *ptr++ = '0' + minute(t)/10;    *ptr++ = '0' + minute(t)%10; *ptr++ = ':';
  *ptr++ = '0' + second(t)/10;    *ptr++ = '0' + second(t)%10; *ptr++ = ' ';

  // print message
  strcpy(ptr, txt);
  ether.udpTransmit(strlen((char *)gPB+UDP_DATA_P));
  Serial.print((char*)gPB+UDP_DATA_P);
  if (txt[strlen(txt)-1] == '\n') Serial.print('\r');
}

//===== setup & loop =====

MilliTimer ntpTimer;
BlinkPlug bl(4);

void setup() {
  Serial.begin(57600);
  Serial.println(HELLO);

  bl.ledOff(3);
  bl.ledOn(2);
  
#if 0
  Serial.print("MAC: ");
  for (byte i = 0; i < 6; ++i) {
          Serial.print(mymac[i], HEX);
          if (i < 5) Serial.print(':');
  }
  Serial.println();
#endif

  if (ether.begin(sizeof Ethernet::buffer, mymac) == 0)
          Serial.println( "Failed to access Ethernet controller");

  for (;;) {
    Serial.println("Setting up DHCP");
    if (ether.dhcpSetup()) break;
    Serial.println( "DHCP failed");
  }

  ether.printIp("My IP: ", ether.myip);
  //ether.printIp("Netmask: ", ether.mymask);
  //ether.printIp("GW IP: ", ether.gwip);
  //ether.printIp("DNS IP: ", ether.dnsip);

  net_setup(NET_ETH_NODE, true);

  bl.ledOff(2);
  
  ntpTimer.poll(1000);
}

void loop() {
  net_packet *pkt;

  // Send an NTP time request
  if (ntpTimer.poll(5000)) {
    ether.ntpRequest(ntpServer, ntpClientPort);
    //Serial.println("Sending NTP request");
    bl.ledOff(1);
  }

  // Keep rf12 moving
  if ((pkt = net_poll())) {
    //Serial.print("Packet: type=");
    //Serial.print(pkt->hdr.type);
    //Serial.print(" len=");
    //Serial.println(pkt->hdr.len);
    switch (pkt->hdr.type) {
    case net_time:
      // ignore
      break;
    case net_msg:
      // send message onwards on ethernet
      char msg[RF12_MAXDATA];
      uint8_t len;
      len = rf12_len - 5; // yuck
      memcpy(msg, pkt->msg.txt, len);
      msg[len] = 0;
      eth_print(rf12_hdr & RF12_HDR_MASK, pkt->msg.time, msg);
      break;
    default:
      Serial.print("Unknown packet type hdr=");
      Serial.print(rf12_hdr,16);
      Serial.print(" len=");
      Serial.print(rf12_len);
      Serial.print(" -- ");
      for (int i=0; i<10; i++) {
        Serial.print(rf12_data[i]);
        Serial.print("/");
        Serial.print((char)rf12_data[i]);
        Serial.print(" ");
      }
      Serial.println();
      break;
    }
  }
  
  // Receive ethernet packages
  int plen = ether.packetReceive();
  ether.packetLoop(plen);
  if (plen > 0) {
    // Check for NTP responses and forward time
    if (ntpProcessAnswer(&time, &frac, ntpClientPort)) {
      // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
      const unsigned long seventyYears = 2208988800UL;     
      // subtract seventy years:
      time = time - seventyYears;

      // set local clock
      setTime(time);
      bl.ledOn(1);

      // Try to send it once on the rf12 radio (don't let it get stale)
      if (rf12_canSend()) {
        struct net_time tbuf = { net_time, time };
        rf12_sendStart(NET_ETH_NODE, &tbuf, sizeof(tbuf));
        eth_print(net_nodeId, time, "Sent time update\n");
      } else {
        eth_print(net_nodeId, time, "Cannot send time update\n");
      }

      // print the time
      time_t t = now();
      Serial.print("The time is: ");
      Serial.print(time);
      Serial.print('.');
      Serial.print(frac);
      Serial.print(" = ");
      Serial.print(month(t));
      Serial.print('/');
      Serial.print(day(t));
      Serial.print('/');
      Serial.print(year(t));
      Serial.print(' ');
      Serial.print(hour(t));
      Serial.print(':');
      Serial.print(minute(t));
      Serial.print(':');
      Serial.print(second(t));
      Serial.println();
    }
  }
  
  
}


