// Copyright (c) 2013 by Thorsten von Eicken
//
// Gate LED transmitter node
// Modulates IR LEDs at 38khz for light barrier at the gate
// LEDs connect to port 1 using a pull-down NPN transistor: output high to turn LEDs
//   on, disable output to turn LEDs off

#include <NetAll.h>
#include <avr/eeprom.h>
#include <avr/sleep.h>

#define LED_PIN 4           // pin6=port1
#define PULSES 10           // number of pulses in a train
#define KHZ38_US 9          // delay for 38khz timing loop in us
#define INTERVAL_MS 36      // interval between pulse trains in ms

Net net(0xD4, true);        // default group_id and low power
Log l, *logger=&l;

MilliTimer burstTimer;

// ISR routine to wake things up when going to sleep
ISR(WDT_vect) { Sleepy::watchdogEvent(); }

// transmit a burst of 38khz light pulses
void burst(byte pulses) {
  noInterrupts();
  for (byte i=0; i<pulses; i++) {
    digitalWrite(LED_PIN, HIGH);
    delayMicroseconds(KHZ38_US);
    digitalWrite(LED_PIN, LOW);
    delayMicroseconds(KHZ38_US+1);
  }
  interrupts();
}

//===== setup & loop =====

static Configured *(node_config[]) = {
  &net, logger, 0
};

void setup() {
  Serial.begin(57600);
  Serial.println(F("***** SETUP: " __FILE__));

  //eeprom_write_word((uint16_t *)0x20, 0xF00D); // reset EEPROM
  config_init(node_config);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  delay(500);
  digitalWrite(LED_PIN, LOW);

  pinMode(14, OUTPUT);
  digitalWrite(14, LOW);

  burstTimer.set(INTERVAL_MS);

  logger->println(F("***** RUNNING: " __FILE__));
}

void loop() {
  int safe;

  if (burstTimer.remaining() < 1) {
    if (burstTimer.poll(INTERVAL_MS)) {
      burst(PULSES);
    }
    if (net.poll()) config_dispatch();

  } else {
    if (net.poll()) {
      config_dispatch();
    }

    // wait for any transmission to finish
    while (!rf12_canSend())
      if (net.poll()) config_dispatch();
    // going low-power
    rf12_sleep(RF12_SLEEP);
    // sleeping here
    rf12_setWatchdog(1);
    set_sleep_mode(SLEEP_MODE_IDLE);          // set the type of sleep mode to use
    sleep_enable();                           // enable sleep mode
    safe = 0;
    while (! rf12_watchdogFired()) {
      digitalWrite(14, HIGH);
      sleep_cpu();                            // nighty-night
      if (++safe > 10) {
        Serial.println("?");
        break;
      }
      digitalWrite(14, LOW);
    }
    sleep_disable();                          // just woke up, disable sleep mode for safety
    // powering up stuff
    rf12_sleep(RF12_WAKEUP);

    if (net.poll()) {
      config_dispatch();
    }
  }

  //rf12_sendWait(2);
  //Sleepy::loseSomeTime(INTERVAL_MS);
  //delay(INTERVAL_MS);

}
