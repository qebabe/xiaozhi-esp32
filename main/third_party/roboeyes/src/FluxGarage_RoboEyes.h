/*
 * FluxGarage RoboEyes for OLED Displays V 1.1.1
 * Draws smoothly animated robot eyes on OLED displays, based on the Adafruit GFX 
 * library's graphics primitives, such as rounded rectangles and triangles.
 *   
 * Copyright (C) 2024-2025 Dennis Hoelscher
 * www.fluxgarage.com
 * www.youtube.com/@FluxGarage
 *
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */


#ifndef _FLUXGARAGE_ROBOEYES_H
#define _FLUXGARAGE_ROBOEYES_H

// Arduino-style type definitions for ESP-IDF compatibility
#include <cstdint>
typedef uint8_t byte;

// Display colors
uint8_t BGCOLOR = 0; // background and overlays
uint8_t MAINCOLOR = 1; // drawings

// For mood type switch
#define DEFAULT 0
#define TIRED 1
#define ANGRY 2
#define HAPPY 3
#define SURPRISED 4
#define SLEEPY 5
#define EVIL 6
#define LOVING 7

// For turning things on or off
#define ON 1
#define OFF 0

// For switch "predefined positions"
#define N 1 // north, top center
#define NE 2 // north-east, top right
#define E 3 // east, middle right
#define SE 4 // south-east, bottom right
#define S 5 // south, bottom center
#define SW 6 // south-west, bottom left
#define W 7 // west, middle left
#define NW 8 // north-west, top left 
// for middle center set "DEFAULT"


// Constructor: takes a reference to the active Adafruit display object (e.g., Adafruit_SSD1327)
// Eg: roboEyes<Adafruit_SSD1327> = eyes(display);
template<typename AdafruitDisplay>
class RoboEyes
{
private:

// Yes, everything is currently still accessible. Be responsible and don't mess things up :)

public:

// Reference to Adafruit display object
AdafruitDisplay *display;

// For general setup - screen size and max. frame rate
int screenWidth = 128; // OLED display width, in pixels
int screenHeight = 64; // OLED display height, in pixels
int frameInterval = 20; // default value for 50 frames per second (1000/50 = 20 milliseconds)
unsigned long fpsTimer = 0; // for timing the frames per second

// For controlling mood types and expressions
bool tired = 0;
bool angry = 0;
bool happy = 0;
bool curious = 0; // if true, draw the outer eye larger when looking left or right
bool surprised = 0; // wide open eyes
bool sleepy = 0; // half-closed eyes
bool evil = 0; // squinted eyes with slant
bool loving = 0; // heart-shaped eyes
bool cyclops = 0; // if true, draw only one eye
bool eyeL_open = 0; // left eye opened or closed?
bool eyeR_open = 0; // right eye opened or closed?


//*********************************************************************************************
//  Eyes Geometry
//*********************************************************************************************

// EYE LEFT - size and border radius
int eyeLwidthDefault = 36;
int eyeLheightDefault = 36;
int eyeLwidthCurrent = eyeLwidthDefault;
int eyeLheightCurrent = 1; // start with closed eye, otherwise set to eyeLheightDefault
int eyeLwidthNext = eyeLwidthDefault;
int eyeLheightNext = eyeLheightDefault;
int eyeLheightOffset = 0;
// Border Radius
byte eyeLborderRadiusDefault = 8;
byte eyeLborderRadiusCurrent = eyeLborderRadiusDefault;
byte eyeLborderRadiusNext = eyeLborderRadiusDefault;

// EYE RIGHT - size and border radius
int eyeRwidthDefault = eyeLwidthDefault;
int eyeRheightDefault = eyeLheightDefault;
int eyeRwidthCurrent = eyeRwidthDefault;
int eyeRheightCurrent = 1; // start with closed eye, otherwise set to eyeRheightDefault
int eyeRwidthNext = eyeRwidthDefault;
int eyeRheightNext = eyeRheightDefault;
int eyeRheightOffset = 0;
// Border Radius
byte eyeRborderRadiusDefault = 8;
byte eyeRborderRadiusCurrent = eyeRborderRadiusDefault;
byte eyeRborderRadiusNext = eyeRborderRadiusDefault;

// EYE LEFT - Coordinates
int eyeLxDefault;
int eyeLyDefault;
int eyeLx;
int eyeLy;
int eyeLxNext;
int eyeLyNext;

// EYE RIGHT - Coordinates
int eyeRxDefault;
int eyeRyDefault;
int eyeRx;
int eyeRy;
int eyeRxNext;
int eyeRyNext;

// BOTH EYES 
// Eyelid top size
byte eyelidsHeightMax = eyeLheightDefault/2; // top eyelids max height
byte eyelidsTiredHeight = 0;
byte eyelidsTiredHeightNext = eyelidsTiredHeight;
byte eyelidsAngryHeight = 0;
byte eyelidsAngryHeightNext = eyelidsAngryHeight;
// Bottom happy eyelids offset
byte eyelidsHappyBottomOffsetMax = (eyeLheightDefault/2)+3;
byte eyelidsHappyBottomOffset = 0;
byte eyelidsHappyBottomOffsetNext = 0;
// Surprised eyes enlargement
byte eyeSurprisedEnlargement = 0;
byte eyeSurprisedEnlargementNext = 0;
// Sleepy eyes reduction
byte eyeSleepyReduction = 0;
byte eyeSleepyReductionNext = 0;
// Evil eye slant
byte eyeEvilSlant = 0;
byte eyeEvilSlantNext = 0;
// Loving heart shape offset
byte eyeLovingOffset = 0;
byte eyeLovingOffsetNext = 0;
// Space between eyes
int spaceBetweenDefault = 10;
int spaceBetweenCurrent = spaceBetweenDefault;
int spaceBetweenNext = 10;


//*********************************************************************************************
//  Macro Animations
//*********************************************************************************************

// Animation - horizontal flicker/shiver
bool hFlicker = 0;
bool hFlickerAlternate = 0;
byte hFlickerAmplitude = 2;

// Animation - vertical flicker/shiver
bool vFlicker = 0;
bool vFlickerAlternate = 0;
byte vFlickerAmplitude = 10;

// Animation - auto blinking
bool autoblinker = 0; // activate auto blink animation
int blinkInterval = 1; // basic interval between each blink in full seconds
int blinkIntervalVariation = 4; // interval variaton range in full seconds, random number inside of given range will be add to the basic blinkInterval, set to 0 for no variation
unsigned long blinktimer = 0; // for organising eyeblink timing

// Animation - idle mode: eyes looking in random directions
bool idle = 0;
int idleInterval = 1; // basic interval between each eye repositioning in full seconds
int idleIntervalVariation = 3; // interval variaton range in full seconds, random number inside of given range will be add to the basic idleInterval, set to 0 for no variation
unsigned long idleAnimationTimer = 0; // for organising eyeblink timing

// Animation - eyes confused: eyes shaking left and right
bool confused = 0;
unsigned long confusedAnimationTimer = 0;
int confusedAnimationDuration = 500;
bool confusedToggle = 1;

// Animation - eyes laughing: eyes shaking up and down
bool laugh = 0;
unsigned long laughAnimationTimer = 0;
int laughAnimationDuration = 500;
bool laughToggle = 1;

// Animation - sweat on the forehead
bool sweat = 0;
byte sweatBorderradius = 3;

// Sweat drop 1
int sweat1XPosInitial = 2;
int sweat1XPos;
float sweat1YPos = 2;
int sweat1YPosMax;
float sweat1Height = 2;
float sweat1Width = 1;

// Sweat drop 2
int sweat2XPosInitial = 2;
int sweat2XPos;
float sweat2YPos = 2;
int sweat2YPosMax;
float sweat2Height = 2;
float sweat2Width = 1;

// Sweat drop 3
int sweat3XPosInitial = 2;
int sweat3XPos;
float sweat3YPos = 2;
int sweat3YPosMax;
float sweat3Height = 2;
float sweat3Width = 1;


//*********************************************************************************************
//  GENERAL METHODS
//*********************************************************************************************

RoboEyes(AdafruitDisplay &disp) {
    display = &disp;
    screenWidth = 128;
    screenHeight = 64;
    frameInterval = 20;
    fpsTimer = 0;
    tired = 0;
    angry = 0;
    happy = 0;
    curious = 0;
    surprised = 0;
    sleepy = 0;
    evil = 0;
    loving = 0;
    cyclops = 0;
    eyeL_open = 0;
    eyeR_open = 0;
    eyeLwidthDefault = 36;
    eyeLheightDefault = 36;
    eyeLwidthCurrent = 36;
    eyeLheightCurrent = 1;
    eyeLwidthNext = 36;
    eyeLheightNext = 36;
    eyeLheightOffset = 0;
    eyeLborderRadiusDefault = 8;
    eyeLborderRadiusCurrent = 8;
    eyeLborderRadiusNext = 8;
    eyeRwidthDefault = 36;
    eyeRheightDefault = 36;
    eyeRwidthCurrent = 36;
    eyeRheightCurrent = 1;
    eyeRwidthNext = 36;
    eyeRheightNext = 36;
    eyeRheightOffset = 0;
    eyeRborderRadiusDefault = 8;
    eyeRborderRadiusCurrent = 8;
    eyeRborderRadiusNext = 8;
    spaceBetweenDefault = 10;
    spaceBetweenCurrent = 10;
    eyeLxDefault = ((screenWidth)-(eyeLwidthDefault+spaceBetweenDefault+eyeRwidthDefault))/2;
    eyeLyDefault = ((screenHeight-eyeLheightDefault)/2);
    eyeLx = eyeLxDefault;
    eyeLy = eyeLyDefault;
    eyeLxNext = eyeLxDefault;
    eyeLyNext = eyeLyDefault;
    eyeRxDefault = eyeLx+eyeLwidthCurrent+spaceBetweenDefault;
    eyeRyDefault = eyeLy;
    eyeRx = eyeRxDefault;
    eyeRy = eyeRyDefault;
    eyeRxNext = eyeRxDefault;
    eyeRyNext = eyeRyDefault;
    eyelidsHeightMax = 18;
    eyelidsTiredHeight = 0;
    eyelidsTiredHeightNext = 0;
    eyelidsAngryHeight = 0;
    eyelidsAngryHeightNext = 0;
    eyelidsHappyBottomOffsetMax = 21;
    eyelidsHappyBottomOffset = 0;
    eyelidsHappyBottomOffsetNext = 0;
    eyeSurprisedEnlargement = 0;
    eyeSurprisedEnlargementNext = 0;
    eyeSleepyReduction = 0;
    eyeSleepyReductionNext = 0;
    eyeEvilSlant = 0;
    eyeEvilSlantNext = 0;
    eyeLovingOffset = 0;
    eyeLovingOffsetNext = 0;
    spaceBetweenNext = 10;
    hFlicker = 0;
    hFlickerAlternate = 0;
    hFlickerAmplitude = 2;
    vFlicker = 0;
    vFlickerAlternate = 0;
    vFlickerAmplitude = 10;
    autoblinker = 0;
    blinkInterval = 1;
    blinkIntervalVariation = 4;
    blinktimer = 0;
    idle = 0;
    idleInterval = 1;
    idleIntervalVariation = 3;
    idleAnimationTimer = 0;
    confused = 0;
    confusedAnimationTimer = 0;
    confusedAnimationDuration = 500;
    confusedToggle = 1;
    laugh = 0;
    laughAnimationTimer = 0;
    laughAnimationDuration = 500;
    laughToggle = 1;
    sweat = 0;
    sweatBorderradius = 3;
    sweat1XPosInitial = 2;
    sweat1XPos = 2;
    sweat1YPos = 2;
    sweat1YPosMax = 0;
    sweat1Height = 2;
    sweat1Width = 1;
    sweat2XPosInitial = 2;
    sweat2XPos = 2;
    sweat2YPos = 2;
    sweat2YPosMax = 0;
    sweat2Height = 2;
    sweat2Width = 1;
    sweat3XPosInitial = 2;
    sweat3XPos = 2;
    sweat3YPos = 2;
    sweat3YPosMax = 0;
    sweat3Height = 2;
    sweat3Width = 1;
};

// Startup RoboEyes with defined screen-width, screen-height and max. frames per second
void begin(int width, int height, byte frameRate) {
	screenWidth = width; // OLED display width, in pixels
	screenHeight = height; // OLED display height, in pixels
  display->clearDisplay(); // clear the display buffer
  display->display(); // show empty screen
  eyeLheightCurrent = 1; // start with closed eyes
  eyeRheightCurrent = 1; // start with closed eyes
  setFramerate(frameRate); // calculate frame interval based on defined frameRate
}

void update(){
  // Limit drawing updates to defined max framerate
  if(millis()-fpsTimer >= frameInterval){
    drawEyes();
    fpsTimer = millis();
  }
}


//*********************************************************************************************
//  SETTERS METHODS
//*********************************************************************************************

// Calculate frame interval based on defined frameRate
void setFramerate(byte fps){
  frameInterval = 1000/fps;
}

// Set color values
void setDisplayColors(uint8_t background, uint8_t main) {
  BGCOLOR = background; // background and overlays, choose 0 for monochrome displays and 0x00 for grayscale displays such as SSD1322
  MAINCOLOR = main; // drawings, choose 1 for monochrome displays and 0x0F for grayscale displays such as SSD1322 (0x0F = maximum brightness)
}

void setWidth(byte leftEye, byte rightEye) {
	eyeLwidthNext = leftEye;
	eyeRwidthNext = rightEye;
  eyeLwidthDefault = leftEye;
  eyeRwidthDefault = rightEye;
}

void setHeight(byte leftEye, byte rightEye) {
	eyeLheightNext = leftEye;
	eyeRheightNext = rightEye;
  eyeLheightDefault = leftEye;
  eyeRheightDefault = rightEye;
}

// Set border radius for left and right eye
void setBorderradius(byte leftEye, byte rightEye) {
	eyeLborderRadiusNext = leftEye;
	eyeRborderRadiusNext = rightEye;
  eyeLborderRadiusDefault = leftEye;
  eyeRborderRadiusDefault = rightEye;
}

// Set space between the eyes, can also be negative
void setSpacebetween(int space) {
  spaceBetweenNext = space;
  spaceBetweenDefault = space;
}

// Set mood expression
void setMood(unsigned char mood)
  {
    switch (mood)
    {
    case TIRED:
      tired=1;
      angry=0;
      happy=0;
      surprised=0;
      sleepy=0;
      evil=0;
      loving=0;
      break;
    case ANGRY:
      tired=0;
      angry=1;
      happy=0;
      surprised=0;
      sleepy=0;
      evil=0;
      loving=0;
      break;
    case HAPPY:
      tired=0;
      angry=0;
      happy=1;
      surprised=0;
      sleepy=0;
      evil=0;
      loving=0;
      break;
    case SURPRISED:
      tired=0;
      angry=0;
      happy=0;
      surprised=1;
      sleepy=0;
      evil=0;
      loving=0;
      break;
    case SLEEPY:
      tired=0;
      angry=0;
      happy=0;
      surprised=0;
      sleepy=1;
      evil=0;
      loving=0;
      break;
    case EVIL:
      tired=0;
      angry=0;
      happy=0;
      surprised=0;
      sleepy=0;
      evil=1;
      loving=0;
      break;
    case LOVING:
      tired=0;
      angry=0;
      happy=0;
      surprised=0;
      sleepy=0;
      evil=0;
      loving=1;
      break;
    default:
      tired=0;
      angry=0;
      happy=0;
      surprised=0;
      sleepy=0;
      evil=0;
      loving=0;
      break;
    }
  }

// Set predefined position
void setPosition(unsigned char position)
  {
    switch (position)
    {
    case N:
      // North, top center
      eyeLxNext = getScreenConstraint_X()/2;
      eyeLyNext = 0;
      break;
    case NE:
      // North-east, top right
      eyeLxNext = getScreenConstraint_X();
      eyeLyNext = 0;
      break;
    case E:
      // East, middle right
      eyeLxNext = getScreenConstraint_X();
      eyeLyNext = getScreenConstraint_Y()/2;
      break;
    case SE:
      // South-east, bottom right
      eyeLxNext = getScreenConstraint_X();
      eyeLyNext = getScreenConstraint_Y();
      break;
    case S:
      // South, bottom center
      eyeLxNext = getScreenConstraint_X()/2;
      eyeLyNext = getScreenConstraint_Y();
      break;
    case SW:
      // South-west, bottom left
      eyeLxNext = 0;
      eyeLyNext = getScreenConstraint_Y();
      break;
    case W:
      // West, middle left
      eyeLxNext = 0;
      eyeLyNext = getScreenConstraint_Y()/2;
      break;
    case NW:
      // North-west, top left
      eyeLxNext = 0;
      eyeLyNext = 0;
      break;
    default:
      // Middle center
      eyeLxNext = getScreenConstraint_X()/2;
      eyeLyNext = getScreenConstraint_Y()/2;
      break;
    }
  }

// Set automated eye blinking, minimal blink interval in full seconds and blink interval variation range in full seconds
void setAutoblinker(bool active, int interval, int variation){
  autoblinker = active;
  blinkInterval = interval;
  blinkIntervalVariation = variation;
}
void setAutoblinker(bool active){
  autoblinker = active;
}

// Set idle mode - automated eye repositioning, minimal time interval in full seconds and time interval variation range in full seconds
void setIdleMode(bool active, int interval, int variation){
  idle = active;
  idleInterval = interval;
  idleIntervalVariation = variation;
}
void setIdleMode(bool active) {
  idle = active;
}

// Set curious mode - the respectively outer eye gets larger when looking left or right
void setCuriosity(bool curiousBit) {
  curious = curiousBit;
}

// Set cyclops mode - show only one eye 
void setCyclops(bool cyclopsBit) {
  cyclops = cyclopsBit;
}

// Set horizontal flickering (displacing eyes left/right)
void setHFlicker (bool flickerBit, byte Amplitude) {
  hFlicker = flickerBit; // turn flicker on or off
  hFlickerAmplitude = Amplitude; // define amplitude of flickering in pixels
}
void setHFlicker (bool flickerBit) {
  hFlicker = flickerBit; // turn flicker on or off
}

// Set vertical flickering (displacing eyes up/down)
void setVFlicker (bool flickerBit, byte Amplitude) {
  vFlicker = flickerBit; // turn flicker on or off
  vFlickerAmplitude = Amplitude; // define amplitude of flickering in pixels
}
void setVFlicker (bool flickerBit) {
  vFlicker = flickerBit; // turn flicker on or off
}

void setSweat (bool sweatBit) {
  sweat = sweatBit; // turn sweat on or off
}


//*********************************************************************************************
//  GETTERS METHODS
//*********************************************************************************************

// Returns the max x position for left eye
int getScreenConstraint_X(){
  return screenWidth-eyeLwidthCurrent-spaceBetweenCurrent-eyeRwidthCurrent;
} 

// Returns the max y position for left eye
int getScreenConstraint_Y(){
 return screenHeight-eyeLheightDefault; // using default height here, because height will vary when blinking and in curious mode
}


//*********************************************************************************************
//  BASIC ANIMATION METHODS
//*********************************************************************************************

// BLINKING FOR BOTH EYES AT ONCE
// Close both eyes
void close() {
	eyeLheightNext = 1; // closing left eye
  eyeRheightNext = 1; // closing right eye
  eyeL_open = 0; // left eye not opened (=closed)
	eyeR_open = 0; // right eye not opened (=closed)
}

// Open both eyes
void open() {
  eyeL_open = 1; // left eye opened - if true, drawEyes() will take care of opening eyes again
	eyeR_open = 1; // right eye opened
}

// Trigger eyeblink animation
void blink() {
  close();
  open();
}

// BLINKING FOR SINGLE EYES, CONTROL EACH EYE SEPARATELY
// Close eye(s)
void close(bool left, bool right) {
  if(left){
    eyeLheightNext = 1; // blinking left eye
    eyeL_open = 0; // left eye not opened (=closed)
  }
  if(right){
      eyeRheightNext = 1; // blinking right eye
      eyeR_open = 0; // right eye not opened (=closed)
  }
}

// Open eye(s)
void open(bool left, bool right) {
  if(left){
    eyeL_open = 1; // left eye opened - if true, drawEyes() will take care of opening eyes again
  }
  if(right){
    eyeR_open = 1; // right eye opened
  }
}

// Trigger eyeblink(s) animation
void blink(bool left, bool right) {
  close(left, right);
  open(left, right);
}


//*********************************************************************************************
//  MACRO ANIMATION METHODS
//*********************************************************************************************

// Play confused animation - one shot animation of eyes shaking left and right
void anim_confused() {
	confused = 1;
}

// Play laugh animation - one shot animation of eyes shaking up and down
void anim_laugh() {
  laugh = 1;
}

//*********************************************************************************************
//  PRE-CALCULATIONS AND ACTUAL DRAWINGS
//*********************************************************************************************

void drawEyes(){

  //// PRE-CALCULATIONS - EYE SIZES AND VALUES FOR ANIMATION TWEENINGS ////

  // Vertical size offset for larger eyes when looking left or right (curious gaze)
  if(curious){
    if(eyeLxNext<=10){eyeLheightOffset=8;}
    else if (eyeLxNext>=(getScreenConstraint_X()-10) && cyclops){eyeLheightOffset=8;}
    else{eyeLheightOffset=0;} // left eye
    if(eyeRxNext>=screenWidth-eyeRwidthCurrent-10){eyeRheightOffset=8;}
    else{eyeRheightOffset=0;} // right eye
  } else {
    eyeLheightOffset=0; // reset height offset for left eye
    eyeRheightOffset=0; // reset height offset for right eye
  }

  // Left eye height
  eyeLheightCurrent = (eyeLheightCurrent + eyeLheightNext + eyeLheightOffset)/2;
  eyeLy+= ((eyeLheightDefault-eyeLheightCurrent)/2); // vertical centering of eye when closing
  eyeLy-= eyeLheightOffset/2;
  // Right eye height
  eyeRheightCurrent = (eyeRheightCurrent + eyeRheightNext + eyeRheightOffset)/2;
  eyeRy+= (eyeRheightDefault-eyeRheightCurrent)/2; // vertical centering of eye when closing
  eyeRy-= eyeRheightOffset/2;


  // Open eyes again after closing them
	if(eyeL_open){
  	if(eyeLheightCurrent <= 1 + eyeLheightOffset){eyeLheightNext = eyeLheightDefault;} 
  }
  if(eyeR_open){
  	if(eyeRheightCurrent <= 1 + eyeRheightOffset){eyeRheightNext = eyeRheightDefault;} 
  }

  // Left eye width
  eyeLwidthCurrent = (eyeLwidthCurrent + eyeLwidthNext)/2;
  // Right eye width
  eyeRwidthCurrent = (eyeRwidthCurrent + eyeRwidthNext)/2;


  // Space between eyes
  spaceBetweenCurrent = (spaceBetweenCurrent + spaceBetweenNext)/2;

  // Left eye coordinates
  eyeLx = (eyeLx + eyeLxNext)/2;
  eyeLy = (eyeLy + eyeLyNext)/2;
  // Right eye coordinates
  eyeRxNext = eyeLxNext+eyeLwidthCurrent+spaceBetweenCurrent; // right eye's x position depends on left eyes position + the space between
  eyeRyNext = eyeLyNext; // right eye's y position should be the same as for the left eye
  eyeRx = (eyeRx + eyeRxNext)/2;
  eyeRy = (eyeRy + eyeRyNext)/2;

  // Left eye border radius
  eyeLborderRadiusCurrent = (eyeLborderRadiusCurrent + eyeLborderRadiusNext)/2;
  // Right eye border radius
  eyeRborderRadiusCurrent = (eyeRborderRadiusCurrent + eyeRborderRadiusNext)/2;
  

  //// APPLYING MACRO ANIMATIONS ////

	if(autoblinker){
		if(millis() >= blinktimer){
		blink();
		blinktimer = millis()+(blinkInterval*1000)+(random(blinkIntervalVariation)*1000); // calculate next time for blinking
		}
	}

  // Laughing - eyes shaking up and down for the duration defined by laughAnimationDuration (default = 500ms)
  if(laugh){
    if(laughToggle){
      setVFlicker(1, 5);
      laughAnimationTimer = millis();
      laughToggle = 0;
    } else if(millis() >= laughAnimationTimer+laughAnimationDuration){
      setVFlicker(0, 0);
      laughToggle = 1;
      laugh=0; 
    }
  }

  // Confused - eyes shaking left and right for the duration defined by confusedAnimationDuration (default = 500ms)
  if(confused){
    if(confusedToggle){
      setHFlicker(1, 20);
      confusedAnimationTimer = millis();
      confusedToggle = 0;
    } else if(millis() >= confusedAnimationTimer+confusedAnimationDuration){
      setHFlicker(0, 0);
      confusedToggle = 1;
      confused=0; 
    }
  }

  // Idle - eyes moving to random positions on screen
  if(idle){
    if(millis() >= idleAnimationTimer){
      eyeLxNext = random(getScreenConstraint_X());
      eyeLyNext = random(getScreenConstraint_Y());
      idleAnimationTimer = millis()+(idleInterval*1000)+(random(idleIntervalVariation)*1000); // calculate next time for eyes repositioning
    }
  }

  // Adding offsets for horizontal flickering/shivering
  if(hFlicker){
    if(hFlickerAlternate) {
      eyeLx += hFlickerAmplitude;
      eyeRx += hFlickerAmplitude;
    } else {
      eyeLx -= hFlickerAmplitude;
      eyeRx -= hFlickerAmplitude;
    }
    hFlickerAlternate = !hFlickerAlternate;
  }

  // Adding offsets for horizontal flickering/shivering
  if(vFlicker){
    if(vFlickerAlternate) {
      eyeLy += vFlickerAmplitude;
      eyeRy += vFlickerAmplitude;
    } else {
      eyeLy -= vFlickerAmplitude;
      eyeRy -= vFlickerAmplitude;
    }
    vFlickerAlternate = !vFlickerAlternate;
  }

  // Cyclops mode, set second eye's size and space between to 0
  if(cyclops){
    eyeRwidthCurrent = 0;
    eyeRheightCurrent = 0;
    spaceBetweenCurrent = 0;
  }

  //// ACTUAL DRAWINGS ////

  display->clearDisplay(); // start with a blank screen

  // Draw basic eye rectangles
  display->fillRoundRect(eyeLx, eyeLy, eyeLwidthCurrent, eyeLheightCurrent, eyeLborderRadiusCurrent, MAINCOLOR); // left eye
  if (!cyclops){
    display->fillRoundRect(eyeRx, eyeRy, eyeRwidthCurrent, eyeRheightCurrent, eyeRborderRadiusCurrent, MAINCOLOR); // right eye
  }

  // Prepare mood type transitions
  if (tired){eyelidsTiredHeightNext = eyeLheightCurrent/2; eyelidsAngryHeightNext = 0;} else{eyelidsTiredHeightNext = 0;}
  if (angry){eyelidsAngryHeightNext = eyeLheightCurrent/2; eyelidsTiredHeightNext = 0;} else{eyelidsAngryHeightNext = 0;}
  if (happy){eyelidsHappyBottomOffsetNext = eyeLheightCurrent/2;} else{eyelidsHappyBottomOffsetNext = 0;}
  if (surprised){eyeSurprisedEnlargementNext = 4;} else{eyeSurprisedEnlargementNext = 0;} // make eyes larger
  if (sleepy){eyeSleepyReductionNext = eyeLheightCurrent/3;} else{eyeSleepyReductionNext = 0;} // reduce eye height
  if (evil){eyeEvilSlantNext = 3;} else{eyeEvilSlantNext = 0;} // slant the eyes
  if (loving){eyeLovingOffsetNext = 2;} else{eyeLovingOffsetNext = 0;} // for heart shapes

  // Draw tired top eyelids 
    eyelidsTiredHeight = (eyelidsTiredHeight + eyelidsTiredHeightNext)/2;
    if (!cyclops){
      display->fillTriangle(eyeLx, eyeLy-1, eyeLx+eyeLwidthCurrent, eyeLy-1, eyeLx, eyeLy+eyelidsTiredHeight-1, BGCOLOR); // left eye 
      display->fillTriangle(eyeRx, eyeRy-1, eyeRx+eyeRwidthCurrent, eyeRy-1, eyeRx+eyeRwidthCurrent, eyeRy+eyelidsTiredHeight-1, BGCOLOR); // right eye
    } else {
      // Cyclops tired eyelids
      display->fillTriangle(eyeLx, eyeLy-1, eyeLx+(eyeLwidthCurrent/2), eyeLy-1, eyeLx, eyeLy+eyelidsTiredHeight-1, BGCOLOR); // left eyelid half
      display->fillTriangle(eyeLx+(eyeLwidthCurrent/2), eyeLy-1, eyeLx+eyeLwidthCurrent, eyeLy-1, eyeLx+eyeLwidthCurrent, eyeLy+eyelidsTiredHeight-1, BGCOLOR); // right eyelid half
    }

  // Draw angry top eyelids 
    eyelidsAngryHeight = (eyelidsAngryHeight + eyelidsAngryHeightNext)/2;
    if (!cyclops){ 
      display->fillTriangle(eyeLx, eyeLy-1, eyeLx+eyeLwidthCurrent, eyeLy-1, eyeLx+eyeLwidthCurrent, eyeLy+eyelidsAngryHeight-1, BGCOLOR); // left eye
      display->fillTriangle(eyeRx, eyeRy-1, eyeRx+eyeRwidthCurrent, eyeRy-1, eyeRx, eyeRy+eyelidsAngryHeight-1, BGCOLOR); // right eye
    } else {
      // Cyclops angry eyelids
      display->fillTriangle(eyeLx, eyeLy-1, eyeLx+(eyeLwidthCurrent/2), eyeLy-1, eyeLx+(eyeLwidthCurrent/2), eyeLy+eyelidsAngryHeight-1, BGCOLOR); // left eyelid half
      display->fillTriangle(eyeLx+(eyeLwidthCurrent/2), eyeLy-1, eyeLx+eyeLwidthCurrent, eyeLy-1, eyeLx+(eyeLwidthCurrent/2), eyeLy+eyelidsAngryHeight-1, BGCOLOR); // right eyelid half
    }

  // Draw happy bottom eyelids
    eyelidsHappyBottomOffset = (eyelidsHappyBottomOffset + eyelidsHappyBottomOffsetNext)/2;
    display->fillRoundRect(eyeLx-1, (eyeLy+eyeLheightCurrent)-eyelidsHappyBottomOffset+1, eyeLwidthCurrent+2, eyeLheightDefault, eyeLborderRadiusCurrent, BGCOLOR); // left eye
    if (!cyclops){ 
      display->fillRoundRect(eyeRx-1, (eyeRy+eyeRheightCurrent)-eyelidsHappyBottomOffset+1, eyeRwidthCurrent+2, eyeRheightDefault, eyeRborderRadiusCurrent, BGCOLOR); // right eye
    }

  // Add sweat drops
    if (sweat){
      // Sweat drop 1 -> left corner
      if(sweat1YPos <= sweat1YPosMax){sweat1YPos+=0.5;} // vertical movement from initial to max
      else {sweat1XPosInitial = random(30); sweat1YPos = 2; sweat1YPosMax = (random(10)+10); sweat1Width = 1; sweat1Height = 2;} // if max vertical position is reached: reset all values for next drop
      if(sweat1YPos <= sweat1YPosMax/2){sweat1Width+=0.5; sweat1Height+=0.5;} // shape grows in first half of animation ...
      else {sweat1Width-=0.1; sweat1Height-=0.5;} // ... and shrinks in second half of animation
      sweat1XPos = sweat1XPosInitial-(sweat1Width/2); // keep the growing shape centered to initial x position
      display->fillRoundRect(sweat1XPos, sweat1YPos, sweat1Width, sweat1Height, sweatBorderradius, MAINCOLOR); // draw sweat drop


      // Sweat drop 2 -> center area
      if(sweat2YPos <= sweat2YPosMax){sweat2YPos+=0.5;} // vertical movement from initial to max
      else {sweat2XPosInitial = random((screenWidth-60))+30; sweat2YPos = 2; sweat2YPosMax = (random(10)+10); sweat2Width = 1; sweat2Height = 2;} // if max vertical position is reached: reset all values for next drop
      if(sweat2YPos <= sweat2YPosMax/2){sweat2Width+=0.5; sweat2Height+=0.5;} // shape grows in first half of animation ...
      else {sweat2Width-=0.1; sweat2Height-=0.5;} // ... and shrinks in second half of animation
      sweat2XPos = sweat2XPosInitial-(sweat2Width/2); // keep the growing shape centered to initial x position
      display->fillRoundRect(sweat2XPos, sweat2YPos, sweat2Width, sweat2Height, sweatBorderradius, MAINCOLOR); // draw sweat drop


      // Sweat drop 3 -> right corner
      if(sweat3YPos <= sweat3YPosMax){sweat3YPos+=0.5;} // vertical movement from initial to max
      else {sweat3XPosInitial = (screenWidth-30)+(random(30)); sweat3YPos = 2; sweat3YPosMax = (random(10)+10); sweat3Width = 1; sweat3Height = 2;} // if max vertical position is reached: reset all values for next drop
      if(sweat3YPos <= sweat3YPosMax/2){sweat3Width+=0.5; sweat3Height+=0.5;} // shape grows in first half of animation ...
      else {sweat3Width-=0.1; sweat3Height-=0.5;} // ... and shrinks in second half of animation
      sweat3XPos = sweat3XPosInitial-(sweat3Width/2); // keep the growing shape centered to initial x position
      display->fillRoundRect(sweat3XPos, sweat3YPos, sweat3Width, sweat3Height, sweatBorderradius, MAINCOLOR); // draw sweat drop
    }

  // Draw new expression effects
  eyeSurprisedEnlargement = (eyeSurprisedEnlargement + eyeSurprisedEnlargementNext)/2;
  eyeSleepyReduction = (eyeSleepyReduction + eyeSleepyReductionNext)/2;
  eyeEvilSlant = (eyeEvilSlant + eyeEvilSlantNext)/2;
  eyeLovingOffset = (eyeLovingOffset + eyeLovingOffsetNext)/2;

  // Surprised: draw larger white circles in the eyes to make them look bigger
  if (surprised && eyeSurprisedEnlargement > 0) {
    int pupilSize = 6 + eyeSurprisedEnlargement;
    display->fillRoundRect(eyeLx + eyeLwidthCurrent/2 - pupilSize/2, eyeLy + eyeLheightCurrent/2 - pupilSize/2,
                          pupilSize, pupilSize, pupilSize/2, BGCOLOR);
    if (!cyclops) {
      display->fillRoundRect(eyeRx + eyeRwidthCurrent/2 - pupilSize/2, eyeRy + eyeRheightCurrent/2 - pupilSize/2,
                            pupilSize, pupilSize, pupilSize/2, BGCOLOR);
    }
  }

  // Sleepy: draw half-closed eyelids
  if (sleepy && eyeSleepyReduction > 0) {
    if (!cyclops) {
      display->fillTriangle(eyeLx, eyeLy + eyeLheightCurrent/2, eyeLx + eyeLwidthCurrent, eyeLy + eyeLheightCurrent/2,
                           eyeLx, eyeLy + eyeLheightCurrent/2 + eyeSleepyReduction, BGCOLOR);
      display->fillTriangle(eyeRx, eyeRy + eyeRheightCurrent/2, eyeRx + eyeRwidthCurrent, eyeRy + eyeRheightCurrent/2,
                           eyeRx + eyeRwidthCurrent, eyeRy + eyeRheightCurrent/2 + eyeSleepyReduction, BGCOLOR);
    } else {
      display->fillTriangle(eyeLx, eyeLy + eyeLheightCurrent/2, eyeLx + eyeLwidthCurrent, eyeLy + eyeLheightCurrent/2,
                           eyeLx + eyeLwidthCurrent/2, eyeLy + eyeLheightCurrent/2 + eyeSleepyReduction, BGCOLOR);
    }
  }

  // Evil: draw slanted lines to make eyes look evil
  if (evil && eyeEvilSlant > 0) {
    display->fillTriangle(eyeLx + eyeLwidthCurrent/4, eyeLy - eyeEvilSlant,
                         eyeLx + eyeLwidthCurrent*3/4, eyeLy + eyeEvilSlant,
                         eyeLx + eyeLwidthCurrent/4, eyeLy + eyeEvilSlant, MAINCOLOR);
    if (!cyclops) {
      display->fillTriangle(eyeRx + eyeRwidthCurrent/4, eyeRy - eyeEvilSlant,
                           eyeRx + eyeRwidthCurrent*3/4, eyeRy + eyeEvilSlant,
                           eyeRx + eyeRwidthCurrent/4, eyeRy + eyeEvilSlant, MAINCOLOR);
    }
  }

  // Loving: draw heart shapes above the eyes
  if (loving && eyeLovingOffset > 0) {
    int heartX = eyeLx + eyeLwidthCurrent/2;
    int heartY = eyeLy - 8 - eyeLovingOffset;
    // Left half of heart
    display->fillTriangle(heartX - 3, heartY + 2, heartX, heartY - 2, heartX + 2, heartY + 2, MAINCOLOR);
    // Right half of heart
    display->fillTriangle(heartX - 2, heartY + 2, heartX, heartY - 2, heartX + 3, heartY + 2, MAINCOLOR);
    // Bottom of heart
    display->fillTriangle(heartX - 3, heartY + 2, heartX + 3, heartY + 2, heartX, heartY + 5, MAINCOLOR);

    if (!cyclops) {
      heartX = eyeRx + eyeRwidthCurrent/2;
      // Left half of heart
      display->fillTriangle(heartX - 3, heartY + 2, heartX, heartY - 2, heartX + 2, heartY + 2, MAINCOLOR);
      // Right half of heart
      display->fillTriangle(heartX - 2, heartY + 2, heartX, heartY - 2, heartX + 3, heartY + 2, MAINCOLOR);
      // Bottom of heart
      display->fillTriangle(heartX - 3, heartY + 2, heartX + 3, heartY + 2, heartX, heartY + 5, MAINCOLOR);
    }
  }

  display->display(); // show drawings on display

} // end of drawEyes method


}; // end of class roboEyes


#endif
