// Copyright (c) 2013 by Thorsten von Eicken
//
// Weather station node
// Uses a bunch of 1-wire based sensors from hobby-boards and well an an anemometer and a
// rain tipping gauge wired straight to the jeenode. Collects temperature, humidity,
// barometric pressure, rain rate, rain event total, wind gust, wind average, and wind
// direction.

#include <NetAll.h>
#include <OwScan.h>
#include <OwTemp2.h>
#include <OwMisc.h>
#include <avr/eeprom.h>

#define OW_PORT        2    // One-wire port
#define MAX_DEV       16
#define MAX_TEMP       4
#define READ_PERIOD   10000L    // how frequently to read sensors (in milliseconds)
#define SCAN_PERIOD  257000L    // how frequently to scan bus (in milliseconds)
#define CWOP_PERIOD   60000L    // how frequently to send cwop packets (in milliseconds)
#define WIND_PERIOD    3000L    // how frequently to calculate wind gust (in milliseconds)

// temperature measurements
float sens_temp[2];					// in degrees farenheit
#define S_TEMP         0    // weather station temperature sensor
#define S_BOX          1    // inside box temperature sensor
// voltage measurements
float sens_volt[4];    			// in volt
#define S_HUM          0		// humidity sensor voltage
#define S_SOLAR        1		// solar sensor voltage
#define S_BARO				 2		// barometer sensor voltage
#define S_VANE				 3		// wind vane voltage
// counters
#define S_ANEMO        0    // anemometer
#define S_RAIN         1    // rain tipping bucket

//Net net(0xD4, false);  // default group_id and normal power
Net net(0, false);       // rf12B disabled
NetTime nettime;
Log::log_config l_conf = {1,0,0,0,0}; // serial only
Log l(l_conf), *logger=&l;
MilliTimer notSet, toggle, debugTimer;
OwScan owScan(OW_PORT+3, MAX_DEV);
OwTemp owTemp(&owScan, MAX_TEMP);
OwMisc owMisc(&owScan);
MilliTimer readTimer, windTimer;
uint32_t scan_last, cwop_last=-50000;
uint8_t wind_speed_max;

#define BLINKS 20
uint8_t ledPin = 15, blinkCnt = 0, blinks = BLINKS;
MilliTimer blinkTimer;

//===== Counters using Pin Change Interrupts

#define CTR_DEBOUNCE		  8		// milliseconds debounce

// use 8-bit counters so we get atomic reads outside the ISRs
uint8_t  sens_ctr[2];				  // running counter
uint32_t sens_ctr_at[2];			// timestamp of last tick

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
  if (now - sens_ctr_at[0] < CTR_DEBOUNCE) return;  // debounce
  uint8_t pin = digitalRead(15);                    // read pin
  if (pin == 0) sens_ctr[0] += 1;                   // incr counter on 1->0 transition
	sens_ctr_at[0] = now;  								      		  // start debounce timer
}

// interrupt routine for PCINT16..23 - ctr2
ISR(PCINT2_vect) {
	digitalWrite(5, digitalRead(3));
  uint32_t now = millis();
  if (now - sens_ctr_at[1] < CTR_DEBOUNCE) return;  // debounce
  uint8_t pin = digitalRead(3);                     // read pin
  if (pin == 0) sens_ctr[1] += 1;                   // incr counter on 1->0 transition
	sens_ctr_at[1] = now;														  // start debounce timer
}

//===== HUMIDITY =====
// The old thermd calibration was: Slope:0.0316, Offset:0.842901

uint8_t calc_humidity(float volt, float temp) {
	// Relative humidity RH = ( V - Offset ) / Slope
	float rh = ( volt - 0.842901 ) / 0.0316;
	// Temp compensation (in farenheit)
	rh = rh / (1.093 - 0.0012 * temp);
	uint8_t val = rh > 100 ? 100 : rh < 0 ? 0 : rh;
#if 1
	logger->print("Humidity: ");
	logger->print(volt);
	logger->print("V ");
	logger->print(val);
	logger->println("%rh");
#endif
	return val;
}

//===== BAROMETER =====
// The old calibration was: Slope=0.6981, Intercept=26.6038

float calc_barometer(float volt) {
	if (volt < 1.0 || volt > 8.5) return NAN; // approx limits of operation
	// Pressure [mmHg] = ( V - Offset ) / Slope
	float press = volt * 0.6981 + 26.6038;
	press *= 33.86; // convert to millibar
	
#if 1
	logger->print("Barometer: ");
	logger->print(volt);
	logger->print("V ");
	logger->print(press);
	logger->println("mbar");
#endif

	return press;
}

//===== RAIN GAUGE =====
// The old calibration was: 0.1in per tip
// Return the number of 1/10th inch of rain since the last query
// see page 28-12 of Derived variables in Davis weather products
// http://www.davisnet.com/product_documents/weather/app_notes/AN_28-derived-weather-variables.pdf

uint32_t rain_last_time;		  // time of last tip, 0 if it's more than EVENT_RESET ago
uint32_t rain_last_calc=1;	  // time of last rain calculation
uint8_t  rain_last_count;			// count at update
uint32_t rain_event_time;			// time of beginning of last rain event
uint32_t rain_event_last;			// last rainfall time in rain event
uint16_t rain_event;					// count since start of rain event
#define RATE_RESET (15*60)	  // (in seconds) reset rain rate after 15 minutes
#define EVENT_RESET (12*60*60)// (in seconds) reset rain event after 12 hours

// return the rain rate in 1/100th in per hour
uint16_t calc_rain_rate() {
	uint32_t delta_cnt, delta_t;
	// capture values since interrupts can change that anytime
  cli();
	uint8_t ctr = sens_ctr[S_RAIN];
	uint32_t at = sens_ctr_at[S_RAIN];
	sei();
	if (at == 0) at = 1; // use 0 as special value
	// see whether we're having tips or not
	if (ctr == rain_last_count) {
		uint32_t now = millis();
		rain_last_calc = now == 0 ? 1 : now;
		// update rain event
		if (rain_event_time != 0 && (now-rain_event_last) > (uint32_t)EVENT_RESET*1000) {
			rain_event_time = 0;
			rain_event = 0;
		}
		// no tips, decay rain rate since last tip up 'til RATE_RESET
		if (rain_last_time == 0) return 0; // dry period
		delta_t = now - rain_last_time;
		if (delta_t > RATE_RESET*1000L) {
			// no rain is a while, stop pretending that it's raining
			rain_last_time = 0;
			logger->println("Rain rate: 0");
			return 0;
		}
		// calculate rate as if a tip occurred now to provide gracefully decaying rain rate
		delta_cnt = 1;
	} else {
		// the gauge tipped, calculate number of tips and time elapsed since last tip prior
		// to previous calculation
		delta_cnt = (uint8_t)(ctr - rain_last_count);
		rain_last_count = ctr;
		if (rain_last_time == 0)
			delta_t = at - rain_last_calc;	// it just started to rain: use time of last calculation
		else
			delta_t = at - rain_last_time;	// it's been raining, use time of prior tip
		rain_last_time = at;
		// update rain event
		if (rain_event_time == 0) {
			rain_event_time = rain_last_calc;
			rain_event = delta_cnt;
		} else {
			rain_event += delta_cnt;
		}
		rain_event_last = at;
	}

#if 1
	logger->print("Rain rate: dcnt=");
	logger->print(delta_cnt);
	logger->print(" dt=");
	logger->print(delta_t);
	logger->print(" 100*in/hr=");
	logger->println((uint16_t)( delta_cnt * 3600000 / delta_t ));
#endif

	// calculate rate in 1/100th in per hour -- x3600000: convert per millisec to per hour
	uint32_t rate = delta_cnt * 3600000 / delta_t;
	return (uint16_t)rate;
}

//===== ANEMOMETER =====
// The old calibration was: Hz * 2.5mph

uint32_t anemo_last_time;
uint8_t  anemo_last_count;

uint32_t anemo_avg_time;			// time avg was last calculated
uint16_t anemo_avg_cnt;				// count when avg was last calculated
uint16_t anemo_avg_cur;				// current count (16-bit counter!)

// calculates wind speed since last time it was called
uint8_t calc_anemo_gust() {
	// capture values since interrupts can change that anytime
  cli();
	uint8_t ctr = sens_ctr[S_ANEMO];
	uint32_t at = sens_ctr_at[S_ANEMO];
	sei();

	// update counter
	uint32_t delta_cnt = (uint8_t)(ctr - anemo_last_count);
	anemo_last_count = ctr;
	anemo_avg_cur += delta_cnt;
	if (delta_cnt == 0) {
		// no wind, avoid leaving anemo_last_time too far into the past
		anemo_last_time = millis();
		return 0;
	}

	// update timer and calculate wind speed
	uint32_t delta_t = at - anemo_last_time;
	anemo_last_time = at;

#if 0
	logger->print("Wind gust: dcnt=");
	logger->print(delta_cnt);
	logger->print(" dt=");
	logger->print(delta_t);
	logger->print(" mph=");
	logger->println((uint8_t)( delta_cnt * 2500 / delta_t ));
#endif

  // calculate wind speed -- x2500: 2.5 mph/hz and milliseconds->seconds
	return (uint8_t)( delta_cnt * 2500 / delta_t );
}

// average calculation must be called after the gust calculation
uint8_t calc_anemo_avg() {
	uint32_t delta_cnt = (uint16_t)(anemo_avg_cur - anemo_avg_cnt);
	anemo_avg_cnt = anemo_avg_cur;
	uint32_t now = millis();
	uint32_t delta_t = now - anemo_avg_time;
	anemo_avg_time = now;

#if 1
	logger->print("Wind avg: dcnt=");
	logger->print(delta_cnt);
	logger->print(" dt=");
	logger->print(delta_t);
	logger->print(" mph=");
	logger->println((uint8_t)( delta_cnt * 2500 / delta_t ));
#endif

	return (uint8_t)( delta_cnt * 2500 / delta_t );
}

//===== WEATHER VANE =====
// The old calibration was: AdjustBy=-0.01, MultiplyBy=1846.15  # 360/(0.205-0.01)

uint16_t calc_vane(float volt) {
	// Direction [degrees] = ( V - AdjustBy ) * MultiplyBy
	int16_t dir = (float)( (volt + -0.01) * 1846.15 - 22.5 );
	dir %= 360;
	
#if 1
	logger->print("Wind Vane: ");
	logger->print(volt);
	logger->print("V ");
	logger->print(dir);
	logger->println(" degrees");
#endif

	return dir;
}

//===== CWOP =====

void print_cwop() {
	int t = sens_temp[S_TEMP]+0.5;
#if 0
	if (t < 10 || t > 120) {
		logger->println("TEMPERATURE OUT OF WHACK!");
		return;
	}
#endif

	// calculate up-front so debug printing doesn't interfere with CWOP line
	uint16_t h = calc_humidity(sens_volt[S_HUM], t);
	float b = calc_barometer(sens_volt[S_BARO]);
	uint16_t d = calc_vane(sens_volt[S_VANE]);
	uint16_t rr = calc_rain_rate();
	uint16_t re = rain_event;
	uint8_t wa = calc_anemo_avg();
	uint8_t wg = wind_speed_max;
	wind_speed_max = 0;

	logger->print("Baro:");
	logger->println(b);

  char buf[8];
  logger->print(F("APTW01,TCPIP*:@000000z3429.95N/11949.07W"));

	// wind direction in degrees
	if (wa > 0 && d >= 0 && d < 360) {
		snprintf(buf, 8, "_%03d", d);
		logger->print(buf);
	} else {
		logger->print("_..."); // wind direction
	}

	// wind speed in mph
	snprintf(buf, 8, "/%03d", wa); // avg wind speed
  logger->print(buf);
	snprintf(buf, 8, "g%03d", wg); // wind gust
  logger->print(buf); // wind gusts

	// temperature in degrees F
  snprintf(buf, 8, "t%03d", t);
  logger->print(buf);

	// rain rate (in/hr)
  snprintf(buf, 8, "r%03d", rr < 1000 ? rr : 999);
  logger->print(buf);

	// rain since start of event
  snprintf(buf, 8, "p%03d", re < 1000 ? re : 999);
  logger->print(buf);

	// barometric pressure in millibar x10
	if (b > 0) {
		snprintf(buf, 8, "b%05d", (uint16_t)(b*10+0.5));
		logger->print(buf);
	}

	// relative humidity in percent
	if (h > 0 && h <= 100) {
		snprintf(buf, 8, "h%02d", h%100); // print h00 for 100%
		logger->print(buf);
	}
	// weather station type
  logger->print("X1w");
  logger->println();
}

//===== setup & loop =====

static Configured *(node_config[]) = {
  &net, logger, &nettime, &owScan, 0
};

void setup() {
  Serial.begin(57600);
  Serial.println(F("***** SETUP: " __FILE__));
  pinMode(ledPin, OUTPUT);
	digitalWrite(ledPin, 0);

  //eeprom_write_word((uint16_t *)0x20, 0xF00D);

  config_init(node_config);
	//uint8_t ll = 0x4; logger->applyConfig(&ll); // rf12b logging only

  owScan.scan((Print*)logger);
	owScan.printDebug((Print*)logger);
  //owScan.swap(0, 1);
	blinks = owScan.getCount()*2;

	ctr_init();

	readTimer.set(1000);
	blinkTimer.set(100);

	delay(100);
	digitalWrite(ledPin, 1);
  logger->println(F("***** RUNNING: " __FILE__));
}

int cnt = 0;
void loop() {
  if (net.poll()) {
    config_dispatch();
  }

	if (windTimer.poll(WIND_PERIOD)) {
		// update instantaneous wind speed
		uint8_t wind_speed = calc_anemo_gust();
		if (wind_speed > wind_speed_max) {
			wind_speed_max = wind_speed;
			logger->print("Anemo: ");
			logger->print(wind_speed);
			logger->print("mph max: ");
			logger->print(wind_speed_max);
			logger->println("mph");
		}
	}

	if (owScan.getCount() > 0 && readTimer.poll(READ_PERIOD)) {
    uint32_t m = millis();
    // check whether we should be scanning first
    uint32_t delta = m - scan_last;
    if (delta >= SCAN_PERIOD) {
      // adjust timer
      while (delta > SCAN_PERIOD) delta -= SCAN_PERIOD;
      scan_last = m - delta;
      // perform the scan
      owScan.scan((Print*)logger);
      blinks = owScan.getCount()*2;
    }

    // now read the sensors
		logger->print(F("Reading sensors @"));
    logger->println(m);

    // temperatures
    while (!owTemp.loop(0))
			delay(1);
		owTemp.printDebug((Print*)logger);
		sens_temp[S_TEMP] = owTemp.get(S_TEMP);
		sens_temp[S_BOX] = owTemp.get(S_BOX);

		// A/D conversions
		for (uint8_t i=0; i<2; i++) {
			int16_t v = owMisc.ds2438GetVad(2+i);
			sens_volt[i*2+0] = (float)v / 1000;
			logger->print("DS2438 @");
			owScan.printAddr(logger, owScan.getAddr(2+i));
			logger->print(" Vad=");
			logger->print(sens_volt[i*2+0]);
			logger->print("V");
			logger->println();

			v = owMisc.ds2438GetVsense(2+i);
			sens_volt[i*2+1] = (float)v * 0.2441 / 1000;
			logger->print("DS2438->");
			owScan.printAddr(logger, owScan.getAddr(2+i));
			logger->print(" Vsense=");
			logger->print(sens_volt[i*2+1]*1000);
			logger->print("mV");
			logger->println();
		}
	}
	{
	  uint32_t m = millis();

    // check whether it's time to send out a CWOP announcement
    uint32_t delta = m - cwop_last;
    if (delta >= CWOP_PERIOD) {
      // adjust timer
      while (delta >= CWOP_PERIOD)
        delta -= CWOP_PERIOD;
      cwop_last = m - delta;
      // print cwop info
      print_cwop();
    }
	}

  // If we don't know the time of day complain about it
  if (timeStatus() != timeSet) {
    if (notSet.poll(51000)) {
      logger->println("Time not set");
    }
  }

  if (blinkTimer.poll(200)) {
		if (blinkCnt & 1) {
			digitalWrite(ledPin, blinkCnt <= blinks ? 0 : 1);
		} else {
			digitalWrite(ledPin, 1); // off
		}
		blinkCnt = (blinkCnt+1) % BLINKS;
	}

	delay(1);
}
