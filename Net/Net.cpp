// Copyright (c) 2013 Thorsten von Eicken
//
// TODO: combine ACKs with responses for packet types that send an immediate response
// TODO: get initial node_id and config CRC from EEPROM

// Reminder: rf12 header (per JeeLib rf12.cpp)
// DST=0: broadcast from named node
// DST=1: unicast to named node
// CTL=0, ACK=0: normal packet, no ack requested
// CTL=0, ACK=1: normal packet, ack requested
// CTL=1, ACK=0: ack packet
// CTL=1, ACK=1: unused
// node 0: reserved for OOK
// node 31: reserved for receive-all-packets

// compile-time definitions to select network implementation
// valid values: NET_NONE, NET_RF12B, NET_SERIAL
#if !defined(NET_NONE) && !defined(NET_SERIAL)
#define NET_RF12B
#endif

#define RF12_19K
#ifdef RF12_19K
// 19Kbps => BW:67khz, dev:45Khz
#define RF12RATE  0xC611 // 19.1kbps
#define RF12BW    0x94C1 // VDI:fast,-97dBm,67khz
#define RF12DEV   0x9820 // 45khz
#endif

#include <JeeLib.h>
#include <alloca.h>
#include <Config.h>
#include <Net.h>
#include <Time.h>

#ifdef NET_SERIAL
#include <Base64.h>
#include <util/crc16.h>
#endif

// Serial Proto States
#define SPS_IDLE   0
#define SPS_START  1 // got start character, reading length
#define SPS_DATA   3 // got length, reading data

// Packet buffers and retries
#define NET_RETRY_MS 100
#define NET_RETRY_MAX  8

// Method for getting RSSI of received packets, this works well by connecting the appropriate
// capacitor on the RF12B module to the SCL/ADC5/PC5 pin on the JeeNode and placing a 1nF capacitor
// across that to ground.
#define RSSI_PIN 5       // read using analogRead

bool node_enabled;       // global variable that enables/disables all code modules
uint8_t node_id;         // this node's rf12 ID

// EEPROM configuration data
typedef struct {
  uint8_t   nodeId;      // rf12 node ID
  bool      enabled;     // whether node is enabled or not
  uint16_t  nodeUuid;    // 16-bit "unique" ID
} net_config;

// Allocate a packet buffer and return a pointer to it
uint8_t *Net::alloc(void) {
#ifndef NET_NONE
	if (!node_enabled || node_id == NET_UNINIT_NODE) return 0;
  if (bufCnt < NET_PKT) return buf[bufCnt].data;
#endif
  return 0;
}

// send the packet at the top of the queue
void Net::doSend(void) {
#ifndef NET_NONE
	return;
#else
  if (bufCnt > 0) {
    uint8_t hdr = buf[0].hdr;
    // Don't ask for an ACK on the last retry
    if (sendCnt+1 >= NET_RETRY_MAX)
      hdr &= ~RF12_HDR_ACK;
    // send as broadcast packet without ACK
#ifdef NET_RF12B
    rf12_sendStart(hdr, &buf[0].data, buf[0].len);
#elifdef NET_SERIAL
#endif

#if DEBUG
    Serial.print(F("Net::doSend: "));
    Serial.print(hdr & RF12_HDR_ACK ? " w/ACK " : " no-ACK ");
    Serial.print((word)&buf[0].data, 16);
    Serial.print(":"); Serial.println(len[0]);
#endif
    // pop packet from queue if we don't expect an ACK
    if ((hdr & RF12_HDR_ACK) == 0) {
      if (bufCnt > 1)
        memcpy(buf, &buf[1], sizeof(net_packet)*(NET_PKT-1));
      bufCnt--;
      sendCnt=0;
    } else {
      sendCnt++;
      sendTime = millis();
    }
  }
#endif
}

// Send a new packet, this is what user code should call
void Net::send(uint8_t len, bool ack) {
#ifndef NET_NONE
	//uint8_t hdr = RF12_HDR_DST | (ack ? RF12_HDR_ACK : 0) | NET_GW_NODE;
  uint8_t hdr = (ack ? RF12_HDR_ACK : 0) | node_id;
  rawSend(len, hdr);
#endif
}

// raw form of send where full header gets passed-in, used by GW to forward from ethernet
void Net::rawSend(uint8_t len, uint8_t hdr) {
#ifndef NET_NONE
  if (bufCnt >= NET_PKT) return; // error?
  buf[bufCnt].len = len;
  buf[bufCnt].hdr = hdr;
  bufCnt++;
  // if there was no packet queued just go ahead and send the new one
  if (bufCnt == 1 && rf12_canSend()) {
    doSend();
  }
#endif
}



// Broadcast a new packet, this is what user code should call
void Net::bcast(uint8_t len) {
#ifndef NET_NONE
  if (bufCnt >= NET_PKT) return; // error?
  buf[bufCnt].len = len;
  buf[bufCnt].hdr = node_id;
  bufCnt++;
  // if there was no packet queued just go ahead and send the new one
  if (bufCnt == 1 && rf12_canSend()) {
    doSend();
  }
#endif
}

void Net::getRssi(void) {
#ifndef NET_NONE
  // go and get the analog RSSI -- TODO: this needs to be configurable
# ifdef RSSI_PIN
    lastRcvRssi = (uint8_t)((analogRead(RSSI_PIN)-300) >> 2);
# else
    lastRcvRssi = 0;
# endif
# endif
}

// Queue an ACK packet
void Net::queueAck(byte dest_node) {
  // ACK packets have CTL=1, ACK=0; and
  // either DST=1 and the dest of the ack, or DST=0 and this node as source
  int8_t hdr = RF12_HDR_CTL;
  hdr |= dest_node == node_id ? node_id : (RF12_HDR_DST|dest_node);
  queuedAck = hdr; // queue the ACK
  queuedRssi = lastRcvRssi;
}

// send a node announcement packet -- using during initialization
void Net::announce(void) {
#ifdef NET_RF12B
  if (rf12_canSend()) {
    struct { uint8_t module; uint16_t uuid; } pkt = { NET_MODULE, nodeUuid };
    rf12_sendStart(node_id, &pkt, sizeof(pkt));
    // send next announcement in 500ms or 20s depending on what we got from EEPROM
    initAt = millis() + (node_id == NET_UNINIT_NODE ? 500 : 20*1000);
    if (initAt == 0) initAt = 1;
    Serial.println(F("Net: sending announcement"));
  }
#elifdef NET_SERIAL
#endif
}

// Poll the rf12 network and return true if a packet has been received
// ACKs are processed automatically (and are expected not to have data,
// but do include the RSSI to make for simple round-trip measurements)
uint8_t Net::poll(void) {
#ifndef NET_NONE
  bool rcv = rf12_recvDone();
  if (rcv && rf12_crc == 0) {
    //Serial.print("Got packet with HDR=0x");
    //Serial.println(rf12_hdr, HEX);
    // at this point either it's a broadcast or it's directed at this node
    if (!(rf12_hdr & RF12_HDR_CTL)) {
      // Normal packet (CTL=0), queue an ACK if that's requested
      // (can't immediately send 'cause we need the buffer)
      getRssi();
      if (rf12_hdr & RF12_HDR_ACK) queueAck(rf12_hdr & RF12_HDR_MASK);
      return rf12_data[0];
    } else if (!(rf12_hdr & RF12_HDR_ACK)) {
      // Ack packet, check that it's for us and that we're waiting for an ACK
      //Serial.print("Got ACK for "); Serial.println(rf12_hdr, 16);
      getRssi();
      if ((rf12_hdr&RF12_HDR_MASK) == node_id && bufCnt > 0 && sendCnt > 0) {
        lastAckRssi = rf12_len == 1 ? rf12_data[0] : 0;
        // pop packet from queue
        if (bufCnt > 1) {
          memcpy(buf, &buf[1], sizeof(net_packet)*(NET_PKT-1));
        }
        bufCnt--;
        sendCnt=0;
      }
    }
  } else if (rcv && rf12_crc != 0) {
    //Serial.println("Got packet with bad CRC");
  }

  reXmit();
#endif
  return 0;
}

void Net::reXmit(void) {
#ifndef NET_NONE
  // If we need to resend the announcement, try to send it
  if (initAt != 0 && millis() >= initAt && node_id != NET_GW_NODE) {
    announce();

  // If we have a queued ack, try to send it
  } else if (queuedAck && rf12_canSend()) {
    rf12_sendStart(queuedAck, &queuedRssi, sizeof(queuedRssi));
    queuedAck = 0;
    queuedRssi = 0;

  // We have a freshly queued message (never sent), try to send it
  } else if (bufCnt > 0 && sendCnt == 0) {
    if (rf12_canSend()) doSend();

  // We have a queued message  that hasn't been acked and it's time to retry
  } else if (bufCnt > 0 && sendCnt > 0 && millis() >= sendTime+NET_RETRY_MS) {
    if (rf12_canSend()) {
      //Serial.print("Rexmit to 0x");
      //Serial.print(buf[0].hdr, 16);
      //Serial.print(" 0x");
      //Serial.print(buf[0].data[0], 16);
      //Serial.println((char *)(buf[0].data+1));
      doSend();
    }

  }
#endif
}

// Constructor
Net::Net(uint8_t group_id, bool lowPower) {
  this->group_id = group_id;
  this->lowPower = lowPower;
  initAt = millis();
  if (initAt == 0) initAt = 1;
  moduleId = NET_MODULE;
  configSize = sizeof(net_config);
}

// ===== Configuration =====

void Net::receive(volatile uint8_t *pkt, uint8_t len) {
#ifndef NET_NONE
  //Serial.println("Got initialization packet!");
  // handle initialization packet (response to announcement)
  // Format: module(8), uuid(16), node_id(8), enabled(8)
  if (len == 4 && *(uint16_t*)(pkt) == nodeUuid) {
    switch (pkt[3]) {
      case 0: node_enabled = false; break;  // force disable (e.g. new node or crc change)
      case 1: node_enabled = true; break;   // force enable
      default: break;                       // whatever EEPROM said (normal case)
    }
    if (node_id != pkt[2]) {
      node_id = pkt[2];
      Serial.print(F("Net: updating node_id to "));
      Serial.println(node_id);
      setNodeId(node_id);
      // write change to eeprom
      net_config eeprom = { node_id, node_enabled, nodeUuid };
      config_write(NET_MODULE, &eeprom);
    }
  }
#endif
}

// ApplyConfig() not just processes the EEPROM config but also initializes the RF12 module
void Net::applyConfig(uint8_t *cf) {
  net_config *eeprom = (net_config *)cf;

  // do we have data from EEPROM or not?
  if (eeprom && eeprom->nodeId != NET_UNINIT_NODE) {
    Serial.print(F("Config: ")); Serial.print(eeprom->nodeId);
    Serial.print(" "); Serial.println(eeprom->enabled);
  } else {
    eeprom = (net_config *)alloca(sizeof(net_config));
    // we need to punch-in some default values
    eeprom->nodeId = NET_UNINIT_NODE;
    eeprom->enabled = false;
    eeprom->nodeUuid = 0x55aa ^ micros();
    config_write(NET_MODULE, eeprom);
  }
  node_id = eeprom->nodeId;
  node_enabled = eeprom->enabled;
  nodeUuid = eeprom->nodeUuid;

  setNodeId(node_id);
  
  // start announcement
  announce();
}

void Net::setNodeId(uint8_t id) {
  node_id = id;

#ifdef NET_NONE
	Serial.println(F("Config Net: RF12B disabled"));
	return;
#else
  // initialize rf12 module
  Serial.print(F("Config Net: node_id="));
  Serial.print(node_id);
  Serial.print(F(" group_id="));
  Serial.print(group_id);
  Serial.print(F(" uuid=0x"));
  Serial.println(nodeUuid, HEX);
  rf12_initialize(node_id, RF12_915MHZ, group_id);
  if (lowPower) {
    Serial.println(F("  low TX power"));
    rf12_control(0x9857); // reduce tx power
    rf12_control(0x94B2); // attenuate receiver 0x94B2 or 0x94Ba
  }
#ifdef RF12_19K
  Serial.println(F("  19kbps"));
  rf12_control(RF12RATE);
  rf12_control(RF12BW);
  rf12_control(RF12DEV);
#endif

	//Serial.println("  rf12 initialized");
  // if we're collecting RSSIs then init the analog pin
#ifdef RSSI_PIN
  analogReference(INTERNAL);
  pinMode(RSSI_PIN, INPUT);
#endif
#endif
}
