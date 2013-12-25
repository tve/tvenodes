// Copyright (c) 2013 by Thorsten von Eicken
//

#include <NetAll.h>
#include <avr/eeprom.h>

#define CNT_PORT       1

#define PERIOD			   4

#define PCI_1					19       // port 2, IRQ
#define PCI_2					 9			 // port 2, AIO


Net net(0xD4, true);  // default group_id and low power
NetTime nettime;
Log l, *logger=&l;
MilliTimer notSet, debugTimer;

//===== Counters using Pin Change Interrupts

#define CTR_DEBOUNCE		10		// milliseconds debounce

// use 8-bit counters so we get atomic reads outside the ISRs
uint8_t ctr_1, ctr_2;
uint32_t ctr_lock1, ctr_lock2;

// Pins:
// Port 2 AIO - PCINT9  - pin15 - ctr1
// Port 2 IRQ - PCINT19 - pin3  - ctr2

void ctr_init(){
  // set pins to inputs with pull-up
  pinMode(3, INPUT);
  digitalWrite(3, HIGH);
  pinMode(15, INPUT);
  digitalWrite(15, HIGH);
  pinMode(5, OUTPUT);
  digitalWrite(5, HIGH);
  // init interrupts
  cli();		            // switch interrupts off
  PCICR  = 0x06;        // Enable PCIE1 & PCIE2 interrupts
  PCMSK1 = 0b00000010;  // enable PCINT9 pin
  PCMSK2 = 0b00001000;  // enable PCINT19 pin
  sei();		            // switch interrupts back on
}

// interrupt routine for PCINT8..14 - ctr1
ISR(PCINT1_vect) {
  uint32_t now = millis();
  if (now - ctr_lock1 < CTR_DEBOUNCE) return; // debounce
  uint8_t pin = digitalRead(15);              // read pin
  if (pin == 0) ctr_1 += 1;                   // incr counter on 1->0 transition
	ctr_lock1 = now;														// start debounce timer
}

// interrupt routine for PCINT16..23 - ctr2
ISR(PCINT2_vect) {
	digitalWrite(5, digitalRead(3));
  uint32_t now = millis();
  if (now - ctr_lock2 < CTR_DEBOUNCE) return; // debounce
  uint8_t pin = digitalRead(3);               // read pin
  if (pin == 0) ctr_2 += 1;                   // incr counter on 1->0 transition
	ctr_lock2 = now;														// start debounce timer
}

//===== setup & loop =====

static Configured *(node_config[]) = {
  &net, logger, &nettime, 0
};

void setup() {
  Serial.begin(57600);
  Serial.println(F("***** SETUP: " __FILE__));

  eeprom_write_word((uint16_t *)0x20, 0xF00D);

  config_init(node_config);

  ctr_init();
	delay(200);
  digitalWrite(5, LOW);
	delay(200);
  digitalWrite(5, HIGH);
	delay(200);
  digitalWrite(5, LOW);

  logger->println(F("***** RUNNING: " __FILE__));
}

void loop() {
  if (net.poll()) {
    config_dispatch();
  }

  // Debug to serial port
  if (debugTimer.poll(PERIOD*900)) {
    logger->print("ctr1:");
    logger->print(ctr_1);
    logger->print(" pin:");
    logger->print(digitalRead(15));
    logger->print(" ctr2:");
    logger->print(ctr_2);
    logger->print(" pin:");
    logger->println(digitalRead(3));
  }

  // If we don't know the time of day, just sit there and wait for it to be set
  if (timeStatus() != timeSet) {
    if (notSet.poll(60000)) {
      logger->println("Time not set");
    }
    return;
  }

}
