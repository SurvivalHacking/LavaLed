// 02/03/2025
// LavaLED -  By Davide Gatti SURVIVAL HACKING  www.gattidavide.it
//
// Short press button = change brightness
// long press button = change preset
// button pressed at startup = forget wifi information
// with Alexa is possible to control brightness and solid color. Black color = Lava effects


// Include and base definition 
#include <stdio.h>
#include <string>
#include <FastLED.h>     // https://github.com/FastLED/FastLED 
#include "fl/json.h"
#include "fl/slice.h"
#include "fx/fx_engine.h"
#include "fx/2d/animartrix.hpp"
#include "fl/ui.h"
#include <WiFi.h> 
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Espalexa.h>    // https://github.com/Aircoookie/Espalexa
#include <EEPROM.h>      // https://github.com/jwrw/ESP_EEPROM 
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager

using namespace fl;

// p configuration
#define LED_PIN      5     // for LED matrix
#define BUTTON_MODE  6     // pushbutton
#define LONG_PRESS_TIME 1000  

#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB
#define BRIGHTNESS 10
#define MATRIX_WIDTH  16
#define MATRIX_HEIGHT 16
#define NUM_LEDS (MATRIX_WIDTH * MATRIX_HEIGHT)
#define MAX_MATRIX_MODE 255
#define EFFECT_TIME 30000    // 30 seconds time for auto change effects

#define FIRST_ANIMATION POLAR_WAVES
#define FASTLED_ANIMARTRIX_LICENSING_AGREEMENT  1

// EEPROM management
#define EEPROM_SIZE 10   // eprom size
#define EEPROM_CONFIGURED_MARKER 0x55
#define EEPROM_MODE_ADDR 1
#define EEPROM_BRIGHT_ADDR 2

CRGB leds[NUM_LEDS];

XYMap xyMap = XYMap::constructRectangularGrid(MATRIX_WIDTH, MATRIX_HEIGHT);

Animartrix animartrix(xyMap, FIRST_ANIMATION);
FxEngine fxEngine(NUM_LEDS);

Espalexa espalexa;

// global variables
int currentMode = 0;
uint8_t currentBright = 10;
uint8_t alexaOff = 0;
uint8_t timeSpeed = 0.1;
uint8_t brightness = 10;
uint8_t fxIndex =1;
uint8_t autoMode =1;
uint8_t firstAuto =1;

// Main setup
void setup() {
   Serial.begin(115200);
   Serial.println("START");
   EEPROM.begin(EEPROM_SIZE);
   
   // read configuration
   if (EEPROM.read(0) != EEPROM_CONFIGURED_MARKER) {
       // first configuration
       EEPROM.write(0, EEPROM_CONFIGURED_MARKER);
       EEPROM.write(EEPROM_MODE_ADDR, 1);    // Preset default
       EEPROM.write(EEPROM_BRIGHT_ADDR, 1);  // Seconds Blink default default
       EEPROM.commit();                      // Important for ESP32!
   }
   
   // Load saved preset
   autoMode = EEPROM.read(EEPROM_MODE_ADDR);
   currentBright = EEPROM.read(EEPROM_BRIGHT_ADDR);
    
   // Pin configuration
   pinMode(BUTTON_MODE, INPUT_PULLUP);
  
   if(digitalRead(BUTTON_MODE)) {
   // Show rest wit red led blinking
      for(int i = 0; i < 5; i++) {
        fill_solid(leds, NUM_LEDS, CRGB::Red);
        FastLED.show();
        delay(100);
        FastLED.clear();
        FastLED.show();
        delay(100);
      }
      Serial.println("Reset WiFi");
      resetWiFi();
   }

   // Inizializzazione FastLED
   FastLED.addLeds<WS2811, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS)
       .setCorrection(TypicalLEDStrip)
       .setScreenMap(xyMap);
   FastLED.setBrightness(currentBright);
   fxEngine.addFx(animartrix);
   animartrix.fxSet(currentMode);

   // WiFiManager Manager
   WiFiManager wm;
   
   // portal timeout (opzional)
   wm.setConfigPortalTimeout(180); // 3 minutes
   
   // Personalize WIFI portale
   wm.setTitle("LAVALED WiFi Setup");
   
   // Show blu led during config
   fill_solid(leds, NUM_LEDS, CRGB::Blue);
   // leds[0] = CRGB::Blue;
   FastLED.setBrightness(10);
   FastLED.show();
   
   // try to start or portal config
   bool res = wm.autoConnect("LAVALED_AP");

   if(!res) {
       Serial.println("Connection failed");
       // Show error wit RED led
       fill_solid(leds, NUM_LEDS, CRGB::Red);
       FastLED.show();
       delay(3000);
       ESP.restart();
   } 
   else {
       // Connected
       Serial.println("WiFi Connected!");
       // Show success with green led
       fill_solid(leds, NUM_LEDS, CRGB::Green);
       FastLED.show();
       delay(1000);
       FastLED.clear();
       FastLED.show();
   }
   
   // Post WIFI configuration
   WiFi.setSleep(false);
   WiFi.setAutoReconnect(true);
   
   if(WiFi.status() == WL_CONNECTED) {
       setupOTA();
       espalexa.addDevice("LAVALED", deviceControl);
       espalexa.begin();
   }
//   updateDisplay();
}
void resetWiFi() {
    WiFiManager wm;
    wm.resetSettings();
    ESP.restart();
}
// Setup OTA
void setupOTA() {
   ArduinoOTA.setHostname("LAVALED");
   
   ArduinoOTA.onStart([]() {
       FastLED.clear();
       FastLED.show();
   });
   
   ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
       uint8_t percentComplete = (progress / (total / 100));
       fill_solid(leds, map(percentComplete, 0, 100, 0, NUM_LEDS), CRGB::Blue);
       FastLED.show();
   });
   
   ArduinoOTA.begin();
}


// Main Loop
void loop() {
   static uint32_t lastUpdate = 0;
   static uint32_t lastButtonCheck = 0;
   static uint32_t lastMatrixUpdate = 0;
   static uint32_t lastPresetChange = 0;
   uint32_t currentMillis = millis();
   CRGB my_color = CRGB::Black;
    

   // OTA Management and network
   if(WiFi.status() == WL_CONNECTED) {
       ArduinoOTA.handle();
   }
  
   // Check button every 50ms
   if(currentMillis - lastButtonCheck > 50) {
       checkButtons();
       lastButtonCheck = currentMillis;
   }

    // Check preset chancge 30s
    if(currentMillis - lastPresetChange > EFFECT_TIME) {
      changeMode();
      lastPresetChange = currentMillis;
    }

   switch (autoMode) {
      case 0:
        my_color = CRGB::Black;
        break;
      case 1:
        // Manage matrix related to mode
        if (alexaOff==0) {                 // if power off through Alexa
            FastLED.setBrightness(currentBright);
            fxEngine.setSpeed(timeSpeed);
            fxEngine.draw(millis(), leds);
            FastLED.show();
        }
        break;
      case 2:
        my_color = CRGB::White;
        break;
      case 3:
        my_color = CRGB::Red;
        break;
      case 4:
        my_color = CRGB::Green;
        break;
      case 5:
        my_color = CRGB::Blue;
        break;
      case 6:
        my_color = CRGB::AntiqueWhite;
        break;
      case 7:
        my_color = CRGB::Gold;
        break;
      case 8:
        FastLED.setBrightness(currentBright);
        break;

   }
   if (firstAuto==1 && autoMode != 1) {   
      fill_solid(leds, NUM_LEDS, my_color);
      FastLED.show();
      firstAuto=0;
   } 

   // Chel Alexa every second
   if(WiFi.status() == WL_CONNECTED && currentMillis - lastUpdate > 2000) {
//       Serial.println("Check Alexa"); 
       espalexa.loop();   
       lastUpdate = currentMillis;
   }

   yield();
}

void checkButtons() {
    static unsigned long pressStartTime = 0;
    static unsigned long lastLongPressTime = 0;
    static bool buttonWasPressed = false;
    static bool longPressActive = false;

    int buttonState = !digitalRead(BUTTON_MODE);

    if (buttonState == LOW) {  // button is pressed
        if (!buttonWasPressed) {  // first press of button detected
            pressStartTime = millis();
            buttonWasPressed = true;
        }
        
        if ((millis() - pressStartTime >= LONG_PRESS_TIME) && !longPressActive) {
            longPressActive = true;  // activate long press function
        }

        if ((millis() - pressStartTime >= LONG_PRESS_TIME) && 
            (millis() - lastLongPressTime >= LONG_PRESS_TIME)) { 
            longPressActive = true;
            lastLongPressTime = millis();  //record last long press event
            autoMode+=1;
            if (autoMode>=8) {
              autoMode=0;    
            }  
            firstAuto=1;
            Serial.print("Auto="); 
            Serial.println(autoMode); 
            EEPROM.write(EEPROM_MODE_ADDR, autoMode);
            EEPROM.commit();
        }

    } else {  // button is released
        if (buttonWasPressed && !longPressActive) {
          currentBright += 10;
          if(currentBright >= 128) currentBright=0;
          FastLED.setBrightness(currentBright);
          Serial.print("Brigthness="); 
          Serial.println(currentBright); 
          EEPROM.write(EEPROM_BRIGHT_ADDR, currentBright);
          EEPROM.commit();
          FastLED.show();
        }
        buttonWasPressed = false;
        longPressActive = false;
    }
}

void changeMode() {
    currentMode += 1;
    if(currentMode >= MAX_MATRIX_MODE) currentMode=0;
    animartrix.fxSet(currentMode);
    Serial.print("Mode="); 
    Serial.println(currentMode); 
}


void deviceControl(uint8_t brightness, uint32_t rgb) {
   if (brightness == 0) {
      alexaOff=1;  
      FastLED.clear();
      FastLED.show();
      return;
   } else {
      alexaOff=0;
   } 

   if  (brightness >= 128) brightness=128; // limit brightness to 128 for reduce current
 
   // Convert Alex RGB color with CRGB for FastLED
   CRGB newColor;
   newColor.r = (rgb >> 16) & 0xFF;
   newColor.g = (rgb >> 8) & 0xFF;
   newColor.b = rgb & 0xFF;

   Serial.print("COLOR="); 
   Serial.println(rgb); 

   // Update colours
   if (rgb == 16777215) {           //16777215=black
     autoMode=1; 
   } else {
     autoMode=8;
     fill_solid(leds, NUM_LEDS, newColor);
   }    

   currentBright=brightness;
   FastLED.setBrightness(currentBright);
   EEPROM.write(EEPROM_BRIGHT_ADDR, currentBright);
   EEPROM.commit();
   FastLED.show();
   firstAuto=0;
}
 



