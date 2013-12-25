TvE-Nodes
=========

Source: http://github.com/tve/tvenodes

This repository contains documentation and the code to drive TvE's
greenhouse and home automation nodes. This project uses Arduino-compatible
JeeNodes (http://jeelabs.org)

This project was started early 2013 and is still in its infancy...

Documentation links:
-------------------

- [Network documentation](Network.md)

HOW-TO
------

All sketches have been compiled on Linux after an install of arduino-1.0.X
and using "make" (as opposed to the arduino IDE). Each sketch contains a
Makefile that references the arduino.mk at the top-level. This Makefile
ensures that the necessary libraries in subdirectories are included
and linked.

TABLE OF CONTENT
----------------

General
- arduino.mk -- makefile to compile a sketch and upload it, supports remote uploading to an arduino hooked up to a BeagleBone Black, see https://groups.google.com/forum/#!category-topic/beagleboard/6am1GKyo60s
- Network.md -- description of the node self-registration and retransmission library
- README.md -- you're reading it...

Libraries
- Net -- network library with self-registration and retransmission
- Net-v1 -- older version of library
- OwMisc -- miscellaneous 1-wire support, including DS2423 counter
- OwRelay -- 1-wire support for DS2406 1-bit output drivers used for relays
- OwScan -- core 1-wire library to scan the bus and enumerate devices
- OwTemp -- older 1-wire library that does scanning and supports DS18B20 temperature sensors
- OwTemp2 -- newer 1-wire libary for temperature sensors, works in conjunction with OwScan
- SlowServo -- library to slow down a servo, i.e. does servo control but ensures that the servo moves slowly, useful when actuating larger & heavier things

Test Sketches
- hello_node -- hack-me up simple test sketch
- test_node -- hack-me up simple test sketch
- counter_node -- test sketch counting pulses on an arduino input
- gateled_node -- test sketch using various IR LEDs to create a light barrier across the driveway
- heatmat_node -- test sketch for a greenhouse seedling heat mat
- owdebug_node -- test sketch to try new one-wire code

Production sketches
- eth_node -- ethernet gateway using a jeenode and ethercard to relay between the RF12B network and UDP to a central server
- gh_node -- greenhouse control with numerous temperature sensors, a fan relay, a heater relay, and control for a vent opening servo
- heating_node -- control for some 1-wire switched relays that turn sub-floor radiant heating circulators on/off
- rssi_node -- display rssi on a graphical LCD
- weather_node -- weather station with an assortment of inherited sensors
- seed_node -- start at a controller for seedling misting
