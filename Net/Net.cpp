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

#include <JeeLib.h>
#include <Config.h>
#include <Net.h>
#include <Time.h>

// Special node IDs
#define NET_GW_NODE      1  // gateway to the IP network
#define NET_UNINIT_NODE 30  // uninitialized node

// Packet buffers and retries
#define NET_RETRY_MS 100
#define NET_RETRY_MAX  8

// Predicate to tell whether the node is initialized (an init packet has been received)
#define INITED (init_id < 0x80)

bool node_enabled;       // global variable that enables/disables all code modules
uint8_t node_id;         // this node's rf12 ID

// EEPROM configuration data
typedef struct {
  uint8_t   nodeId;             // rf12 node ID
  bool      enabled;            // whether node is enabled or not
} net_config;

// Allocate a packet buffer and return a pointer to it
uint8_t *Net::alloc(void) {
  if (bufcnt < NET_PKT) return buf[bufcnt].data;
  return 0;
}

// send the packet at the top of the queue
void Net::doSend(void) {
  if (bufcnt > 0) {
    uint8_t hdr = buf[0].hdr;
    // Don't ask for an ACK on the last retry
    if (sendCnt+1 >= NET_RETRY_MAX)
      hdr &= ~RF12_HDR_ACK;
    // send as broadcast packet without ACK
    rf12_sendStart(hdr, &buf[0].data, buf[0].len);
#if DEBUG
    Serial.print("Net::doSend: ");
    Serial.print(hdr & RF12_HDR_ACK ? " w/ACK " : " no-ACK ");
    Serial.print((word)&buf[0].data, 16);
    Serial.print(":"); Serial.println(len[0]);
#endif
    // pop packet from queue if we don't expect an ACK
    if ((hdr & RF12_HDR_ACK) == 0) {
      if (bufcnt > 1)
        memcpy(buf, &buf[1], sizeof(net_packet)*(NET_PKT-1));
      bufcnt--;
      sendCnt=0;
    } else {
      sendCnt++;
      sendTime = millis();
    }
  }
}

// Send a new packet, this is what user code should call
void Net::send(uint8_t len, bool ack) {
  if (bufcnt >= NET_PKT) return; // error?
  buf[bufcnt].len = len;
  buf[bufcnt].hdr = RF12_HDR_DST | (ack ? RF12_HDR_ACK : 0) | NET_GW_NODE;
  bufcnt++;
  // if there was no packet queued just go ahead and send the new one
  if (bufcnt == 1 && rf12_canSend()) {
    doSend();
  }
}

// Broadcast a new packet, this is what user code should call
void Net::bcast(uint8_t len) {
  if (bufcnt >= NET_PKT) return; // error?
  buf[bufcnt].len = len;
  buf[bufcnt].hdr = node_id;
  bufcnt++;
  // if there was no packet queued just go ahead and send the new one
  if (bufcnt == 1 && rf12_canSend()) {
    doSend();
  }
}

// Send or queue an ACK packet
void Net::sendAck(byte nodeId) {
  // ACK packets have CTL=1, ACK=0; and
  // either DST=1 and the dest of the ack, or DST=0 and this node as source
  int8_t hdr = RF12_HDR_CTL;
  hdr |= nodeId == node_id ? node_id : RF12_HDR_DST|node_id;
  if (rf12_canSend()) {
    rf12_sendStart(hdr, 0, 0);
#if DEBUG
    //Serial.print("Sending ACK to "), Serial.println(nodeId);
#endif
  } else {
    queuedAck = hdr; // queue the ACK
  }
}

// send a node announcement packet -- using during initialization
void Net::announce(void) {
  // calculate the announcement packet content
  if (!INITED) {
    init_id = 0x80 + 33;    // TODO: generate random value
    init_crc = 0xAA55;      // TODO: calculate CRC across EEPROM config
    init_at = millis();
  }
  // send the packet
  if (rf12_canSend()) {
    struct { uint8_t id; uint16_t crc; } pkt = { init_id, init_crc };
    rf12_sendStart(NET_UNINIT_NODE, &pkt, sizeof(pkt));
    // sned next announcement in 500ms or 20s depending on what we got from EEPROM
    init_at = millis() + (init_id & 0x80 ? 500 : 20*1000);
  }
}

// handle initialization packet (response to announcement)
void Net::handleInit(void) {
  init_id = rf12_data[4];
  switch (rf12_data[5]) {
    case 0: node_enabled = false; break;  // force disable (e.g. new node or crc change)
    case 1: node_enabled = true; break;   // force enable
    default: break;                       // whatever EEPROM said (normal case)
  }
  init_at = 0;
}

// Poll the rf12 network and return true if a packet has been received
// ACKs are processed automatically (and are expected not to have data)
uint8_t Net::poll(void) {
  if (rf12_recvDone() && rf12_crc == 0) {
    //Serial.println("Got some packet");
    // at this point either it's a broadcast or it's directed at this node
    if (!(rf12_hdr & RF12_HDR_CTL)) {
      // Normal packet (CTL=0), send an ACK if that's requested
      if (rf12_hdr & RF12_HDR_ACK) sendAck(rf12_hdr & RF12_HDR_MASK);
      // Handle initialization packet
      if ((rf12_hdr & RF12_HDR_MASK) == NET_UNINIT_NODE) {
        if (!INITED &&
            rf12_data[0] == init_id &&
            rf12_data[1] == (init_crc>>8) &&
            rf12_data[2] == (init_crc&0xFF)) {
          handleInit();
        }
      } else {
        return rf12_data[0];
      }
    } else if (!(rf12_hdr & RF12_HDR_ACK)) {
      // Ack packet, check that it's for us and that we're waiting for an ACK
      //Serial.print("Got ACK for "); Serial.println(rf12_hdr, 16);
      if ((rf12_hdr&RF12_HDR_MASK) == node_id && bufcnt > 0 && sendCnt > 0) {
        // pop packet from queue
        if (bufcnt > 1) {
          memcpy(buf, &buf[1], sizeof(net_packet)*(NET_PKT-1));
        }
        bufcnt--;
        sendCnt=0;
      }
    }
  }

  // If we need to resend the announcement, try to send it
  if (!INITED && millis() >= init_at) {
    announce();

  // If we have a queued ack, try to send it
  } else if (queuedAck && rf12_canSend()) {
    rf12_sendStart(queuedAck, 0, 0);
    queuedAck = 0;

  // We have a freshly queued message (never sent), try to send it
  } else if (bufcnt > 0 && sendCnt == 0) {
    if (rf12_canSend())
      doSend();
    //else
    //  Serial.print("#");

  // We have a queued message  that hasn't been acked and it's time to retry
  } else if (bufcnt > 0 && sendCnt > 0 && millis() >= sendTime+NET_RETRY_MS) {
    if (rf12_canSend())
      doSend();
    //else
    //  Serial.print(".");

  }

  return 0;
}

// Constructor
Net::Net(uint8_t group_id, bool lowPower) {
  this->group_id = group_id;
  this->lowPower = lowPower;
  init_id = 0x80;
  init_at = -1;
}

// ===== Configuration =====

uint8_t Net::moduleId(void) { return NET_MODULE; }
uint8_t Net::configSize(void) { return sizeof(net_config); }
void Net::receive(volatile uint8_t *pkt, uint8_t len) { return; } // this is never called :-)

// ApplyConfig() not just processes the EEPROM config but also initializes the RF12 module
void Net::applyConfig(uint8_t *cf) {
  net_config *eeprom = (net_config *)cf;
  
  // do we have data from EEPROM or not?
  if (eeprom) {
    // yes
    init_id = eeprom->nodeId;  // the ID used within announcement pkt is our normal node ID
  } else {
    // nope, note that init_id defaults to 0x80 and will get something random in the lower bits
    // we need to punch-in some default values
    eeprom->nodeId = NET_UNINIT_NODE;
    eeprom->enabled = false;
  }
  node_id = eeprom->nodeId;
  node_enabled = eeprom->enabled;
  
  // initialize rf12 module
  Serial.print("Config Net: node_id=");
  Serial.println(node_id);
  rf12_initialize(node_id, RF12_915MHZ, group_id);
  if (lowPower) {
    Serial.println("  reducing tx power and rx gain");
    rf12_control(0x9857); // reduce tx power
    rf12_control(0x94B2); // attenuate receiver 0x94B2 or 0x94Ba
  }

  // start announcement
  announce();
}
