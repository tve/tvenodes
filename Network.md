TvE-Nodes Network Documentation
===============================

Node initialization
-------------------

Node initialization is what happens when a node powers up and joins the network. The rf12 group_id must be set in the node's code (i.e., it's predefined), but everything else is dynamically set. When initialization completes the node and management server can communicate with each other and that's about it. For a freshly added node the next step then is configuration.

Each node is in one of the following states at any time, and the state is saved in EEPROM so it is unchanged at the next power-up):
- uninitialized: the EEPROM doesn't contain proper config info, the node doesn't have a node id
- disabled: the node is initialized and has a node id but is in a disabled state where it turns everything off (or into a safe state) and only acts on configuration packets
- enabled: the node is initialized, has a node id and operates fully/normally

To initialize a freshly added node the following steps occur:
- initial boot-up: the node comes up, detects that its EEPROM is not initialized and puts itself in uninitialized state
- initialization: the management server initializes the node by setting its node id, which puts the node in initialized but disabled state
- configuration: the management server (and user) configures all the settings on the node and eventually enables it
- operating: the node is in the enabled state and runs normally; the management server can change configuration settings and can also put the node in disabled state, if that's somehow necessary

The initialization occurs as follows:
- an uninitialized node comes up and announces itself to the management server, the announcement contains:
  - sketch name so a user can identify what this is
  - list of code modules included in the sketch (think of this as list of plugs)
- the management server can configure a node by assigning it an id
- each code module can similarly include a configuration method that stores config in eeprom and can be set/changed by the management server
- after initial config on future power-ups each code configures itself from eeprom, the management server can "unconfigure" a node to reset it
- on future power-ups an initialized node goes either into enabled or disabled state based on the corresponding EEPROM setting (i.e. how it was before power went out); except that it first checks whether a code change occurred that requires new config (for example, a code module was added or removed) and if so it goes into disabled state

When a node powers up it sends out node announcement packets about once a minute until it receives a response from the management server. The announcement packet payload contains:
- 1-byte node id, which is either the previously assigned ID, or 0x80 + a random 7-bit value
- 2-byte configuration CRC that allows the management server to detect whether something has changed, e.g. the code may have been updated and now include new modules/features
- 10 bytes sketch name (zero padded) to allow a human to figure out what this thing is
- a list of multiple code module descriptors (think of this as list of plugs) that describe what's in the node to the management server and allows it to reuse code to communicate with the node and possibly to create a UI. Each descriptor consists of:
  - 1-byte identifier with 6 bits of type and 2 bits of instance (multiple of same code module) or version (to distinguish multiple versions)
  - 1-byte character to identify the instance
  - 1-byte of arbitrary data

The module identifier is also used during normal operation to address packets, so if a module 0x30 announces itself, then it uses 0x30 later on for data packets. Some examples of code module identifiers:
- Some nodes have several one-wire temperature sensors on one pin, some nodes may use multiple pins with several sensors each. The code module type effectively says "one-wire temperature sensors on one pin", the module instance distinguishes the pin when sevaral are used, the character identifies the pin to the user (could be '1', '2', .. or something mnemonic), the last byte has the number of sensors on the pin.
- Some nodes have a relay that turns an appliance on/off based on a temperature setting, for each appliance there is a temperature setting, a current state, and commands to change the temperature setting and "manual" on/off overrides. The 6 type bits say "temperature controlled appliance", the 2 version bits enumerate the appliances, the character identifies the appliance, the last byte says whether the temperature setting is a min or a max (i.e. appliance is on below vs. above the temperature)
- Some nodes have a "complex" control algorithm with multiple settings and such, the version bits can be used to handle upgrades seamlessly

The management server must respond to an announcement packet with an initialization packet. The init packet either sets the rf12 node id or tells the node that it's OK and can stop announcing itself. If a node id is set in the init packet, the node transitions to the disabled state.

Node Configuration
------------------

Node configuration is about setting a node's operating parameters, such as at what temperature it should turn on the heater or what color the LED strip should take on. You can think of node initialization as establishing the transport layer and node configuration as configuring the application layer running on each node.

A number of ingredients must come together to support the configuration of each node:
- The announcement packet tells the management server which code modules are included in each node, it also assigns these code modules an order/position
- Each code module receives an EEPROM offset to store its config, these offsets are in the same order as the announcement packet
- Each code module should implement methods to read/write the config using custom messages, any changes need to update the operating variables in RAM as well as the stored config in EEPROM
- The network library implements methods to read/write the EEPROM config for each code module, but this is more of a back-up because in-RAM settings are not updated
- The management server needs to have code or a description that let it:
  - map code module id to configuration structs and commands to read/write that
  - map node ids to the list of code modules and how all that relates to UI/automation

In order to detect code module changes that require reconfiguration the node stores a checksum of its announcement packet in EEPROM and if that changes it powers up into the disabled state.

Packet structure
----------------

There are 5 classes of packets:
 1. Node announcement packets
 1. Node initialization packets
 1. Packets from a node to the management server (and possibly to other nodes)
 1. Packets from the management server to a node
 1. Acknowledgement packets

The packets have the following format:

````
              +-----+-----------+-----+---------+----------------------------+
              | CTL | DST       | ACK | Node_id | Payload                    |
              +-----+-----------+-----+---------+----------------------------+
Announcement: |  0  | 0=bcast   |  0  | 30      | node_id | 16-bit CRC | ... |
              +-----+-----------+-----+---------+----------------------------+
Init          |  0  | 0=bcast   |  0  | 30      | node_id | 16-bit CRC | ... |
              +-----+-----------+-----+---------+----------------------------+
From mgmt srv |  0  | 1=unicast | 0/1 | target  | code module id | ...       |
              +-----+-----------+-----+---------+----------------------------+
To mgmt srv   |  0  | 0=bcast   |  0  | source  | code module id | ...       |
              +-----+-----------+-----+---------+----------------------------+
ACK           |  1  | 1=unicast |  0  | target  | null                       |
              +-----+-----------+-----+---------+----------------------------+
````

Node initialization packets
---------------------------

Nodes send out announcement packets frequently (every second) when they first boot until
they receive a corresponding intialization packet. The packet structure is:
 - DST=0 (bcast)
 - ACK=0 (no ACK)
 - Node_id=30/self (30 if EEPROM is not initialized, node_id from EEPROM otherwise)
 - module=0 (net_module)
 - 16-bit node uuid (generated randomly and stored in EEPROM)

The packet structure of the initialization packet sent as response from the management
server is:
 - DST=1 (unicast)
 - ACK=0
 - Node_id=same as in announcement
 - module=0
 - 16-bit node uuid, same as in announcement
 - 8-bit new node id
 - 8-bit enable flag (0=disable node, 1-enable node, 2-use value in EEPROM)
