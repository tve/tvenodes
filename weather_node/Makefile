HOST      = core
LIBRARIES = OneWire
LOCALLIBS = Net
SERIALDEV = net:$(HOST):2000
LOCALFLAGS= -DLOG_NORF12B -DNET_NONE
include ../arduino.mk

run: upload size
	nc $(HOST) 2001

#	cu -l /dev/ttyUSB0 -s 57600
