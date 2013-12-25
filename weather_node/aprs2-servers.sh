#!/bin/bash
for h in `curl -s http://www.aprs2.net/APRServe2.txt | tr -d "\r" | egrep '(US|CA)$' | cut -d: -f1`; do
  nc -w 3 -4 -z $h 8080 && echo $h
done
