HOST      = bbb
LIBRARIES = OneWire
#LOCALLIBS = Net
SERIALDEV = net:$(HOST):2000
include ../arduino.mk
#LINKFLAGS += -Wl,-M

run: upload size
	nc $(HOST) 2001
