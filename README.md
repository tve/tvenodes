TvE-Nodes
=========

Source: http://github.com/tve/tvenodes

This repository contains documentation and the code to drive TvE's
greenhouse and home automation nodes. This project uses Arduino-compatible
JeeNodes (http://jeelabs.org) and much of it concerns itself with the
wireless communication between the nodes and a central management servers,
plus the web UI of the management server.

This project was started early 2013 and is still in its infancy...

Documentation links:
-------------------

- [Network documentation](Network.md)

HOW-TO
------

All sketches have been compiled on Linux after an install of arduino-1.0.3
and using "make" (as opposed to the arduino IDE). Each sketch contains a
Makefile that references the arduino.mk at the top-level. This Makefile
ensures that the necessary libraries in subdirectories are included
and linked.
