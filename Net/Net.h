// Copyright (c) 2013 Thorsten von Eicken
//
// RFG12 network transport layer.
// Supports 5 classes of packets: node announcements, node initialization
// packets to gateway, packets from gateway, inter-node broadcasts, and ACKs. Manages the
// announcement of the node at power-up and includes automatic dispatch of received messages.

#ifndef Net_h
#define Net_h

// Special node IDs
#define NET_GW_NODE      1  // gateway to the IP network
#define NET_UNINIT_NODE 30  // uninitialized node

// rf12 packet minus the leading group byte
typedef struct {
  uint8_t   hdr;
  uint8_t   len;
  uint8_t   data[RF12_MAXDATA];
} net_packet;
#define NET_PKT        3                // number of packet buffers allocated

// Global variables that are managed by the network module
extern bool node_enabled;       // global variable that enables/disables all code modules
extern uint8_t node_id;         // this node's rf12 ID

class Net : public Configured {
  // variables related to sending packets
  net_packet buf[NET_PKT];      // buffer for outgoing packets
  uint8_t bufcnt;               // number of outgoing packets in buffer
  uint8_t sendCnt;              // number of transmissions of last packet
  uint32_t sendTime;            // when last packet was sent (for retries)
  uint8_t queuedAck;            // header of queued ACK

  // variables related to initialization
  uint8_t init_id;              // node id used in announcement&init packets
  uint16_t init_crc;            // crc of configuration sent in init packet
  uint32_t init_at;             // when last init packet was sent
  uint8_t group_id;             // rf12 group ID
  bool lowPower;                // whether to reduce tx power and rx gain

  void doSend(void);
  void sendAck(byte nodeId);
  void announce(void);
  void handleInit(void);

public:
  // Constructor, doesn't init any HW yet; the HW is configured by applyConfig() which is
  // called by the EEPROM config system after the EEPROM is read
  // @group_id is the rf12 group_id to use
  // @lowPower if true reduces tx power and rx gain for testing purposes
  Net(uint8_t group_id=0xD4, bool lowPower=false);

  // alloc allocates a packet buffer, allowing multiple packets to be queued. This comes
  // in handy particularly when data packets and debug message packets are sent in rapid
  // succession.
  // @return pointer to buffer (corresponding to data payload) or null if no buffer is available
  uint8_t *alloc(void);

  // send the last allocated buffer as a packet to the management server. Note that
  // this means that each alloc call must be followed by a send or bcast call.
  // @len is the length of the payload
  // @ack says whether an ACK should be requested
  void send(uint8_t len, bool ack=true);

  // bcast broadcasts the last allocated buffer as a packet to all nodes.
  // @len is the length of the payload
  void bcast(uint8_t len);

  // poll must be called in the arduino loop() function to keep the network moving (both
  // send and receive)
  // @return the first byte (module_id) of a received packet, 0 when there's no packet
  uint8_t poll(void);

  // Configuration methods
  virtual uint8_t moduleId(void);
  virtual uint8_t configSize(void);
	virtual void applyConfig(uint8_t *);
	virtual void receive(volatile uint8_t *pkt, uint8_t len);
};

extern Net net; // must be allocated in sketch's main

#endif // Net_h
