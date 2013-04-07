// C 2013 Thorsten von Eicken

// rf12 Network Packets and handling

#ifndef Net_h
#define Net_h

// Ids for all the nodes
#define NET_ETH_NODE   1
#define NET_GH_NODE    2
#define NET_SEED_NODE  3
#define NET_HELLO_NODE 4

#define NET_MSG_MAX (RF12_MAXDATA-5) // max txt length for net_msg

// Message types
typedef enum {
  net_time=1, net_msg, net_temp,
} net_type;

// Message formats
struct net_hdr {
  uint8_t type;
};
struct net_time {
  uint8_t type;
  uint32_t time;
};
struct net_msg {
  uint8_t type;
  uint32_t time;
  char txt[NET_MSG_MAX];
};
struct net_temp {
  uint8_t type;
  uint32_t time;
  struct {
    char name[4];
    float temp;
  } v[7];
};
typedef union {
  struct net_hdr  hdr;
  struct net_time time;
  struct net_msg  msg;
  struct net_temp temp;
} net_packet;

extern uint8_t net_nodeId;

void net_setup(uint8_t id, bool lowPower=false);      // set-up the rf12 communication
net_packet *net_alloc(void);     // return a pointer to a packet buffer
void net_send(uint8_t len);      // send previously allocated packet, with specified data length
net_packet *net_poll(void);      // poll the network, return true if packet has been recv'd

class Console : public Print {
  uint8_t buffer[NET_MSG_MAX];
  uint8_t ix;
  void send(void);
public:
  Console() { ix = 0; }
  virtual size_t write (uint8_t v) {
    if (ix >= NET_MSG_MAX) send();
    buffer[ix++] = v;
    if (v == 012) send();
    return 1;
  }
};

#endif // Net_h
