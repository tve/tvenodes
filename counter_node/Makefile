LIBRARIES = OneWire
LOCALLIBS = Net
SERIALDEV=net:core:2000
include ../arduino.mk

run: upload size
	nc core 2001
