LIBRARIES = OneWire
LOCALLIBS = Net
include ../arduino.mk

run: upload size
	cu -l /dev/ttyUSB0 -s 57600
