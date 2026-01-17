//***********************************************************************************************
//  This example shows how to read an LDR sensor (also called photoresistor) 
//  to open the RoboEyes when it's bright and close the eyes in the dark.
//  Check the dedicated Youtube short:
//  https://youtube.com/shorts/vDrkL4PK0HA?feature=share
//
//  Parts needed:
//  - Micro controller, e.g. ESP32 C3 Supermini or other Arduino compatible board
//  - Oled display, SSD1306
//  - LDR, such as the GL5528
//  - Fixed resistor, 10 kΩ
//  - Jumper Wires
//
//  Wiring:
//  3.3V ----[ LDR ]---- A0 ----[ 10kΩ Resistor ]---- GND
//  
//  Published in August 2025 by Dennis Hoelscher, FluxGarage
//  www.youtube.com/@FluxGarage
//  www.fluxgarage.com
//
//***********************************************************************************************

// Display Driver
#include <Adafruit_SSD1306.h>
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// RoboEyes
#include <FluxGarage_RoboEyes.h>
RoboEyes<Adafruit_SSD1306> roboEyes(display); // create RoboEyes instance


void setup() {
  // Startup Serial
  Serial.begin(115200);

  // Startup OLED Display
  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C or 0x3D
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }

  // Startup robo eyes
  roboEyes.begin(SCREEN_WIDTH, SCREEN_HEIGHT, 100);
  roboEyes.setPosition(DEFAULT);
}


void loop() {
  readLDR(A0, 3200); // read LDR sensor at pin A0 and define threshold
  roboEyes.update(); // update eyes drawings
}



// READ LDR SENSOR / PHOTO RESISTOR
void readLDR(int ldrPin, int ldrThreshold) {
  int ldrValue = analogRead(ldrPin);
  Serial.println(ldrValue);
  if (ldrValue > ldrThreshold) {
    roboEyes.open(); // Open the eyes if it's brighter than the threshold
  }
  else {
    roboEyes.close(); // Close the eyes if it's darker than the threshold
  }
}
