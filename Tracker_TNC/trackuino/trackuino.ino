/* trackuino copyright (C) 2010  EA5HAV Javi
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

// Mpide 22 fails to compile Arduino code because it stupidly defines ARDUINO 
// as an empty macro (hence the +0 hack). UNO32 builds are fine. Just use the
// real Arduino IDE for Arduino builds. Optionally complain to the Mpide
// authors to fix the broken macro.
#if (ARDUINO + 0) == 0
#error "Oops! We need the real Arduino IDE (version 22 or 23) for Arduino builds."
#error "See trackuino.pde for details on this"

// Refuse to compile on arduino version 21 or lower. 22 includes an 
// optimization of the USART code that is critical for real-time operation
// of the AVR code.
#elif (ARDUINO + 0) < 180
#error "Oops! We need Arduino 1.8 or higher"
#error "See trackuino.pde for details on this"

#endif


// Trackuino custom libs
#include "config.h"
#include "afsk_avr.h"
#include "afsk_pic32.h"
#include "aprs.h"
#include "buzzer.h"
#include "gps.h"
#include "pin.h"
#include "power.h"
#include "sensors_avr.h"
#include "sensors_pic32.h"
#include "dra818.h"

#include <Arduino.h>

// Module constants
static const uint32_t VALID_POS_TIMEOUT = 2000;  // ms

// Module variables
static int32_t next_aprs = 0;

// Current FIX ok
//bool nowfix = false;
// At least one FIX
//bool onefix = true;

SoftwareSerial gpsSerial(GPS_RX, GPS_TX);
SoftwareSerial fakeSerial(EMPTYA, EMPTYB);

void setup()
{
  // Start Serial
  Serial.begin(230400);
  
  //LED Pin
  pinMode(LED_PIN, OUTPUT);
  pin_write(LED_PIN, LOW);
  
  // Due to a problem on V1.00 PCB we cannot use D12
  //pinMode(12, INPUT);

#ifdef DEBUG_RESET
  Serial.println("RESET");
#endif

  // init all components
  buzzer_setup();
  afsk_setup();
  gps_setup();
  sensors_setup();
  dorji_sequence();

  fakeSerial.begin(GPS_BAUDRATE);
  gpsSerial.begin(GPS_BAUDRATE);
  
  #ifdef DEBUG_GPS
      Serial.println("GPS Started");
  #endif
  
#ifdef DEBUG_SENS
  Serial.print("Ti=");
  Serial.print(sensors_int_lm60());
  Serial.print(", Te=");
  Serial.print(sensors_ext_lm60());
  Serial.print(", Vin=");
  Serial.println(sensors_vin());
#endif

#ifdef DEBUG_PROT
    // Transmit empty data just to check AF OUT
    next_aprs = millis() + 3000;
#else
  // Do not start until we get a valid time reference
  // for slotted transmissions.
  if (APRS_SLOT >= 0) {
    do {
      while (! gpsSerial.available())
        power_save();
    } while (! gps_decode(gpsSerial.read()));
    
    next_aprs = millis() + 1000 *
      (APRS_PERIOD - (gps_seconds + APRS_PERIOD - APRS_SLOT) % APRS_PERIOD);
  }
  else {
    next_aprs = millis();
  }  
#endif
  // TODO: beep while we get a fix, maybe indicating the number of
  // visible satellites by a series of short beeps?
}

void get_pos()
{ 
  gpsSerial.listen();
  
  // Get a valid position from the GPS
  int valid_pos = 0;
  uint32_t timeout = millis();

#ifdef DEBUG_GPS
  Serial.println("\nget_pos()");
#endif

  gps_reset_parser();

  do {
    if (Serial.available())
      valid_pos = gps_decode(gpsSerial.read());
  } while ( (millis() - timeout < VALID_POS_TIMEOUT) && ! valid_pos) ;

  if (valid_pos) {
    if (gps_altitude > BUZZER_ALTITUDE) {
      buzzer_off();   // In space, no one can hear you buzz
    } else {
      buzzer_on();
    }
  }
}

void loop()
{
  // Time for another APRS frame
  if ((int32_t) (millis() - next_aprs) >= 0) {
    get_pos();
    fakeSerial.listen();
    delay(500);
    aprs_send();
    
    #ifdef DEBUG_PROT
    next_aprs += 3000;
    #else
    next_aprs += APRS_PERIOD * 1000L;
    #endif
    while (afsk_flush()) {
      power_save();
    }

#ifdef DEBUG_MODEM
    // Show modem ISR stats from the previous transmission
    afsk_debug();
#endif

  } else {
    #ifdef DEBUG_GPS
      Serial.println("GPS Receive");
    #endif
    
    gpsSerial.listen();
    
    // Discard GPS data received during sleep window
    while (gpsSerial.available()) {
      gpsSerial.read();
    }
  }

  power_save(); // Incoming GPS data or interrupts will wake us up
}
