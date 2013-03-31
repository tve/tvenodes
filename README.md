TvE-Nodes
========

TvE's Arduino/JeeNodes used to control the greenhouse and more...


Network communication
---------------------

The network communication occurs as follows:
- the JeeLib group ID defines an rf12b network (std stuff)
- each rf12b network has a gateway to the IP network, either using an EtherCard or using serial communication with some host (or using some wifi shield)
- by and large all communication uses a star topology between individual nodes and the gateway node
- the gateway transparently relays packets between the management server and rf12b (except for some queuing and ACKs), the gateway doesn't do much on its own

(need details...)

Node configuration
------------------
Each node is in one of the following states at any time, and the state is saved in EEPROM so it is unchanged at the next power-up):
- uninitialized: the EEPROM doesn't contain proper config info, the node doesn't have a node id
- disabled: the node has a node id but is in a disabled state where it turns everything off (or into a safe state) and only acts on configuration packets
- enabled: the node has a node id and if configured and operates fully/normally
To configure a node the following steps occur:
- initial boot-up: the node comes up, detects that its EEPROM is not initialized, initializes it and puts itself in uninitialized state
- initialization: the management server initializes the node by setting its node id, which puts the node in disabled state
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

The node announcement packet is sent out once a minute by a booting node until it receives a response from the management server. The announcement has the form:
- 1-byte node id (either the previously assigned ID, or 0x80 + a random 7-bit value)
- 2-byte configuration CRC to detect node code changes
- 10 bytes sketch name (zero padded)
- multiple code module identifiers, each consisting of:
  - 1-byte identifier with 6 bits of type and 2 bits of version or instance (multiple of same code module)
  - 1-byte character to identify the instance
  - 1-byte of arbitrary data
The module identifier is also used to address packets, so if a module 0x30 announces itself, then it uses 0x30 later on for data packets. Some examples of code module identifiers:
- Some nodes have several one-wire temperature sensors on one pin, some nodes may use multiple pins with several sensors each. The type says "one-wire temperature", the instance distinguishes the pin, the character identifies the pin to the user, the last byte has the number of sensors on the pin.
- Some nodes have a relay that turns an appliance on/off based on a temperature setting, for each appliance there is a temperature setting, a current state, and commands to change the temperature setting and "manual" on/off overrides. The 6 type bits say "temperature controlled appliance", the 2 version bits enumerate the appliances, the character identifies the appliance, the last byte says whether the temperature setting is a min or a max (i.e. appliance is on below vs. above the temperature)
- Some nodes have a "complex" control algorithm with multiple settings and such, the version bits can be used to handle upgrades seamlessly

The management server must respond to an announcement packet with an initialization packet. The init packet either sets the rf12 node id or tells the node that it's OK and can stop announcing itself. If a node id is set in the init packet, the node transitions to the disabled state.

Configuration
-------------

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

There are 6 classes of packets:
1. Node announcement packets
1. Node initialization packets
1. Packets from a node to the management server
1. Packets from the management server to a node
1. Inter-node Broadcasts
1. Acknowledgement packets

The following is in common across all classes:
- the gateway node is defined to have node_id=1
- the use of ACK is optional on a per-packet basis

Node announcement packets:
- DST=0=broadcast
- CTL=0,ACK=1
- node_id=31 (identifies this class of packet)
- byte #1: node id or randomly picket 7-bit id
- bytes #2-3: configuration CRC

Node initialization packets (the response to an announcement packet):
- DST=0=broadcast
- CTL=0,ACK=1
- node_id=31 (identifies this class of packet)
- bytes #1-#3: same as found in announcement packet and used to address init packet

Packets to the management server are formatted as follows:
- DST=1=unicast
- node_id=1 (the id of the gateway)
- ACK:varies
- byte #1: code module id

Packets from the management server are formatted as follows:
- DST=1=unicast
- node_id=target node
- ACK:varies
- byte #1: code module id

Inter-node broadcasts are sparingly to allow nodes to inform others about stuff, for example, a temperature sensed by one node may be used by another to control something:
- DST=0=broadcast
- CTL=0,ACK=0
- node_id=source node, this means that nodes using the value probably need to have a config setting to know which node to listen to
- byte #1: code module id

ACKs:
- DST=1=unicast
- CTL=1,ACK=0
- node_id: target node
- the packet may be otherwise empty, or it may contain the same payload as a normal packet to/from the management server

