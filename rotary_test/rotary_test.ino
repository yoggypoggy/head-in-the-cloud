#include <Adafruit_NeoPixel.h>
#define LED_PIN     4
#define NUM_LEDS    30
#define BUTTON_PIN  12 
#define outputA 32
#define outputB 14

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
uint8_t light = 50;

int counter = 0; 
int aState;
int aLastState;  
 void setup() { 
  Serial.println("start");
  strip.begin();
  strip.show();

  pinMode (outputA,INPUT);
  pinMode (outputB,INPUT);
  
   Serial.begin (9600);
  // Reads the initial state of the outputA
  aLastState = digitalRead(outputA);   
} 
void loop() { 
  for (uint16_t i = 0; i < NUM_LEDS; i++) {
    strip.setPixelColor(i, strip.Color(light, light, light));
  }
  strip.show();
  aState = digitalRead(outputA); // Reads the "current" state of the outputA
  // If the previous and the current state of the outputA are different, that means a Pulse has occured
   if (aState != aLastState){     
    // If the outputB state is different to the outputA state, that means the encoder is rotating clockwise
    if (digitalRead(outputB) != aState) { 
       counter ++;
       updateStripBrightness(true);
     } else {
       counter --;
       updateStripBrightness(false);
     }
     Serial.print("Position: ");
     Serial.println(counter);
  } 
  aLastState = aState; // Updates the previous state of the outputA with the current state
}

void updateStripBrightness(bool clockwise){
  int change = 1;
  if (!clockwise) {
    change = -1;
  }
  if (light > 0 && change == -1) {
    light += change;
  }
  if (light < 255 && change == 1) {
    light += change;
  }
}