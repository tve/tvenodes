LIBRARIES = OneWire
LOCALLIBS = Net
SERIALDEV=net:core:2000
include ../arduino.mk

run: upload size
	nc core 2001
#	cu -l /dev/ttyUSB0 -s 57600
