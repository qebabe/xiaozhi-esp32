//***********************************************************************************************
//  This example shows how to use the basic methods of the FluxGarage Robo Eyes library. 
//
//  Hardware: You'll need a breadboard, a micro controller such as ESP32, an SPI oled display    
//  with 1322 chip and some jumper wires.
//  
//  Published in may 2025 by Dennis Hoelscher, FluxGarage
//  www.youtube.com/@FluxGarage
//  www.fluxgarage.com
//
//***********************************************************************************************

#include <SSD1322_for_Adafruit_GFX.h> // -> Display driver, download here: https://github.com/venice1200/SSD1322_for_Adafruit_GFX

// Used for software SPI
#define OLED_CLK 4
#define OLED_MOSI 6

// Used for software or hardware SPI
#define OLED_CS 7
#define OLED_DC 9

// Used for I2C or SPI
#define OLED_RESET 8

// hardware SPI
Adafruit_SSD1322 display(256, 64, &SPI, OLED_DC, OLED_RESET, OLED_CS);

#include <FluxGarage_RoboEyes.h>
RoboEyes<Adafruit_SSD1322> roboEyes(display); // create RoboEyes instance
#define SCREEN_WIDTH 256 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels


void setup()   {                
  Serial.begin(9600);
  //while (! Serial) delay(100);
  Serial.println("SSD1327 OLED test");
  
  if ( ! display.begin(0x3D) ) {
     Serial.println("Unable to initialize OLED");
     while (1) yield();
  }

  // Startup robo eyes
  roboEyes.begin(SCREEN_WIDTH, SCREEN_HEIGHT, 100); // screen-width, screen-height, max framerate
  roboEyes.setDisplayColors(0x00, 0x0F); // Most dark/bright values for grayscale oleds, such as the SSD1322
   
  // Define some automated eyes behaviour
  roboEyes.setAutoblinker(ON, 3, 2); // Start auto blinker animation cycle -> bool active, int interval, int variation -> turn on/off, set interval between each blink in full seconds, set range for random interval variation in full seconds
  roboEyes.setIdleMode(ON, 2, 2); // Start idle animation cycle (eyes looking in random directions) -> turn on/off, set interval between each eye repositioning in full seconds, set range for random time interval variation in full seconds
  
  // Define eye shapes, all values in pixels
  //roboEyes.setWidth(36, 36); // byte leftEye, byte rightEye
  //roboEyes.setHeight(36, 36); // byte leftEye, byte rightEye
  //roboEyes.setBorderradius(8, 8); // byte leftEye, byte rightEye
  //roboEyes.setSpacebetween(10); // int space -> can also be negative

  // Cyclops mode
  //roboEyes.setCyclops(ON); // bool on/off -> if turned on, robot has only on eye

  // Define mood, curiosity and position
  //roboEyes.setMood(DEFAULT); // mood expressions, can be TIRED, ANGRY, HAPPY, DEFAULT
  //roboEyes.setPosition(DEFAULT); // cardinal directions, can be N, NE, E, SE, S, SW, W, NW, DEFAULT (default = horizontally and vertically centered)
  //roboEyes.setCuriosity(ON); // bool on/off -> when turned on, height of the outer eyes increases when moving to the very left or very right

  // Set horizontal or vertical flickering
  //roboEyes.setHFlicker(ON, 2); // bool on/off, byte amplitude -> horizontal flicker: alternately displacing the eyes in the defined amplitude in pixels
  //roboEyes.setVFlicker(ON, 2); // bool on/off, byte amplitude -> vertical flicker: alternately displacing the eyes in the defined amplitude in pixels

  // Play prebuilt oneshot animations
  //roboEyes.anim_confused(); // confused - eyes shaking left and right
  //roboEyes.anim_laugh(); // laughing - eyes shaking up and down

} // end of setup


void loop() {
  roboEyes.update(); // update eyes drawings
 // Dont' use delay() here in order to ensure fluid eyes animations.
 // Check the AnimationSequences example for common practices.
}


