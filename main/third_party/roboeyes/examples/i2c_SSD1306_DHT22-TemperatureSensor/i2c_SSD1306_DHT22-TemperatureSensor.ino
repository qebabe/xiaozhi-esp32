// Example testing sketch for various DHT humidity/temperature sensors
// Written by ladyada, public domain

// REQUIRES the following Arduino libraries:
// - DHT Sensor Library: https://github.com/adafruit/DHT-sensor-library
// - Adafruit Unified Sensor Lib: https://github.com/adafruit/Adafruit_Sensor

//***********************************************************************************************
//  Adafruit's example has been extended for being used with the FluxGarage RoboEyes, 
//  showing different robot face expressions and animations in relation to the temperature  
//  variable "t". Beside the DHT22 sensor, add an I2C SSD1306 OLED display to get started.
//  Check the dedicated Youtube short: https://youtube.com/shorts/xLKj-iLbz8U
//
//  Published in August 2025 by Dennis Hoelscher, FluxGarage
//  www.youtube.com/@FluxGarage
//  www.fluxgarage.com
//
//***********************************************************************************************

#include "DHT.h"

#define DHTPIN 2     // Digital pin connected to the DHT sensor
// Feather HUZZAH ESP8266 note: use pins 3, 4, 5, 12, 13 or 14 --
// Pin 15 can work but DHT must be disconnected during program upload.

// Uncomment whatever type you're using!
//#define DHTTYPE DHT11   // DHT 11
#define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321
//#define DHTTYPE DHT21   // DHT 21 (AM2301)

// Connect pin 1 (on the left) of the sensor to +5V
// NOTE: If using a board with 3.3V logic like an Arduino Due connect pin 1
// to 3.3V instead of 5V!
// Connect pin 2 of the sensor to whatever your DHTPIN is
// Connect pin 3 (on the right) of the sensor to GROUND (if your sensor has 3 pins)
// Connect pin 4 (on the right) of the sensor to GROUND and leave the pin 3 EMPTY (if your sensor has 4 pins)
// Connect a 10K resistor from pin 2 (data) to pin 1 (power) of the sensor

// Initialize DHT sensor.
// Note that older versions of this library took an optional third parameter to
// tweak the timings for faster processors.  This parameter is no longer needed
// as the current DHT reading algorithm adjusts itself to work on faster procs.
DHT dht(DHTPIN, DHTTYPE);


#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#include <FluxGarage_RoboEyes.h> // https://github.com/FluxGarage/RoboEyes
RoboEyes<Adafruit_SSD1306> roboEyes(display); // create RoboEyes instance

// EVENT TIMER
unsigned long eventTimer; // will save the timestamps

void setup() {
  Serial.begin(9600);

  // Startup OLED Display
  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C or 0x3D
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }

  // Startup dht sensor lib
  dht.begin();
  //Serial.println(F("DHTxx test!"));

  // Startup robo eyes
  roboEyes.begin(SCREEN_WIDTH, SCREEN_HEIGHT, 100); // screen-width, screen-height, max framerate
  roboEyes.setPosition(DEFAULT); // eye position should be middle center
  roboEyes.setAutoblinker(ON, 3, 2); // start auto blinker
  
  display.setRotation(2); // Rotate the display by 180 degrees - this makes sense if your display is mounted upside down

  eventTimer = millis(); // start event timer from here
  
}

void loop() {
 roboEyes.update(); // update eyes drawings
 // dont' use delay() here in order to ensure fluid eyes animations

  // Measure temperature every 5 seconds
  if(millis() >= eventTimer+5000){
    // Reading temperature or humidity takes about 250 milliseconds!
    // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
    float h = dht.readHumidity();
    // Read temperature as Celsius (the default)
    float t = dht.readTemperature();
    // Read temperature as Fahrenheit (isFahrenheit = true)
    float f = dht.readTemperature(true);

    // Check if any reads failed and exit early (to try again).
    if (isnan(h) || isnan(t) || isnan(f)) {
      Serial.println(F("Failed to read from DHT sensor!"));
      return;
    }

    // Compute heat index in Fahrenheit (the default)
    float hif = dht.computeHeatIndex(f, h);
    // Compute heat index in Celsius (isFahreheit = false)
    float hic = dht.computeHeatIndex(t, h, false);

    /*Serial.print(F("Humidity: "));
    Serial.print(h);
    Serial.print(F("%  Temperature: "));
    Serial.print(t);
    Serial.print(F("°C "));
    Serial.print(f);
    Serial.print(F("°F  Heat index: "));
    Serial.print(hic);
    Serial.print(F("°C "));
    Serial.print(hif);
    Serial.println(F("°F"));
    */
    Serial.println(t);
    
    if (t < 15){
      // Robot is freezing if temperature is below 15° Celsius
      roboEyes.setMood(TIRED);
      roboEyes.setHFlicker(ON, 2);
    }
    else if (t > 25){
      // Robot is sweating if temperature is above 25° Celsius
      roboEyes.setMood(TIRED);
      roboEyes.setSweat(ON);
      roboEyes.setPosition(S);
    }
    else {
      // Robot's comfort zone: temperature is between 15° and 25° Celsius
      roboEyes.setMood(DEFAULT); // neutral face expression
      roboEyes.setPosition(DEFAULT); // eye position: middle center
      roboEyes.setHFlicker(OFF);
      roboEyes.setSweat(OFF);
    }

    eventTimer = millis(); // reset timer
  }
} // end of main loop

