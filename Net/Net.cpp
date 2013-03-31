// C 2013 Thorsten von Eicken

// rf12 Network Packets and handling
//
// rf12 header:
// DST=0: broadcast from named node
// DST=1: unicast to named node
// CTL=0, ACK=0: normal packet, no ack requested
// CTL=0, ACK=1: normal packet, ack requested
// CTL=1, ACK=0: ack packet
// CTL=1, ACK=1: unused

#include <JeeLib.h>
#include <Net.h>
#include <Time.h>

#define NET_RETRY_MS 100
#define NET_RETRY_MAX 8
#define NET_PKT        2                // size of packet buffer

static net_packet net_buf[NET_PKT];     // buffer for outgoing packets
static uint8_t net_len[NET_PKT];        // data length of packets in net_buf
static uint8_t net_bufsz = 0;           // number of outgoing packets in buffer
static uint8_t net_sendCnt = 0;         // number of transmissions of last packet
static uint32_t net_sendTime = 0;       // when last packet was sent (for retries)
uint8_t net_nodeId = -1;                // local node id
static uint8_t net_queuedAck = 0;       // header of queued ACK

// Initialize rf12 communication
void net_setup(uint8_t id, bool lowPower) {
  net_nodeId = id;
  rf12_initialize(id, RF12_915MHZ);
  if (lowPower) {
    Serial.println("Net: reducing tx power and rx gain");
    rf12_control(0x9857); // reduce tx power
    rf12_control(0x94B2); // attenuate receiver 0x94B2 or 0x94Ba
  }
}

// Allocate a packet buffer and return a pointer to it
net_packet *net_alloc(void) {
  if (net_bufsz < NET_PKT) return &net_buf[net_bufsz];
  return 0;
}

// send the packet at the top of the queue
static void net_doSend(void) {
  if (net_bufsz > 0) {
    if (net_buf[0].hdr.type == net_time ||
        net_sendCnt+1 >= NET_RETRY_MAX) {
      // send as broadcast packet without ACK
      rf12_sendStart(net_nodeId, &net_buf[0], net_len[0]);
      //Serial.print("net_doSend: no-ACK 0x"); Serial.print((word)&net_buf[0], 16);
      //Serial.print(":"); Serial.println(net_len[0]);
      // pop packet from queue
      if (net_bufsz > 1) {
        memcpy(net_buf, &net_buf[1], sizeof(net_packet)*(NET_PKT-1));
        memcpy(net_len, &net_len[1], NET_PKT-1);
      }
      net_bufsz--;
      net_sendCnt=0;
    } else {
      // send as broadcast packet and request ACK
      rf12_sendStart(net_nodeId|RF12_HDR_ACK, &net_buf[0], net_len[0]);
      //Serial.print("net_doSend: sending 0x"); Serial.print((word)&net_buf[0], 16);
      //Serial.print(":"); Serial.println(net_len[0]);
      net_sendCnt++;
      net_sendTime = millis();
    }
  }
}

// Send a new packet, this is what user code should call
void net_send(uint8_t len) {
  if (net_bufsz >= NET_PKT) return; // error?
  net_len[net_bufsz] = len;
  net_bufsz++;
  // if there was no packet queued just go ahead and send the new one
  if (net_bufsz == 1 && rf12_canSend()) {
    net_doSend();
  }
}

// Send or queue an ACK packet
static void net_sendAck(byte nodeId) {
  // ACK packets have CTL=1, ACK=0; and
  // either DST=1 and the dest of the ack, or DST=0 and this node as source
  int8_t hdr = RF12_HDR_CTL;
  hdr |= nodeId == net_nodeId ? nodeId : RF12_HDR_DST|nodeId;
  if (rf12_canSend()) {
    rf12_sendStart(hdr, 0, 0);
    //Serial.print("Sending ACK to "), Serial.println(nodeId);
  } else {
    net_queuedAck = hdr; // queue the ACK
  }
}



// Poll the rf12 network and return true if a packet has been received
// ACKs are processed automatically (and are expected not to have data)
net_packet *net_poll(void) {
  if (rf12_recvDone() && rf12_crc == 0) {
    //Serial.println("Got some packet");
    // at this point either it's a broadcast or it's directed at this node
    if (!(rf12_hdr & RF12_HDR_CTL)) {
      // Normal packet (CTL=0)
      if (rf12_hdr & RF12_HDR_ACK) net_sendAck(rf12_hdr & RF12_HDR_MASK);
      return (net_packet *)rf12_data;
    } else if (!(rf12_hdr & RF12_HDR_ACK)) {
      // Ack packet, check that it's for us and that we're waiting for an ACK
      //Serial.print("Got ACK for "); Serial.println(rf12_hdr, 16);
      if ((rf12_hdr&RF12_HDR_MASK) == net_nodeId && net_bufsz > 0 && net_sendCnt > 0) {
        // pop packet from queue
        if (net_bufsz > 1) {
          memcpy(net_buf, &net_buf[1], sizeof(net_packet)*(NET_PKT-1));
          memcpy(net_len, &net_len[1], NET_PKT-1);
        }
        net_bufsz--;
        net_sendCnt=0;
      }
    }
  }

  // If we have a queued ack, try to send it
  if (net_queuedAck && rf12_canSend()) {
    rf12_sendStart(net_queuedAck, 0, 0);
    net_queuedAck = 0;

  // We have a freshly queued message (never sent), try to send it
  } else if (net_bufsz > 0 && net_sendCnt == 0) {
    if (rf12_canSend())
      net_doSend();
    //else
    //  Serial.print("#");

  // We have a queued message  that hasn't been acked and it's time to retry
  } else if (net_bufsz > 0 && net_sendCnt > 0 && millis() >= net_sendTime+NET_RETRY_MS) {
    if (rf12_canSend())
      net_doSend();
    //else
    //  Serial.print(".");

  }

  return 0;
}

void Console::send(void) {
  net_packet *pkt = net_alloc();
  if (pkt) {
    pkt->msg.type = net_msg;
    pkt->msg.time = now();
    memcpy(&pkt->msg.txt, buffer, ix);
    net_send(ix+5); // +5 for type byte and for time
    buffer[ix] = 0;
    Serial.print("Console: ");
    Serial.print((char *)buffer);
  } else {
    //Serial.println("*** out of rf12 buffers ***");
  }
  ix = 0;
}
