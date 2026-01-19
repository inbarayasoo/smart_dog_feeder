#include <Adafruit_NeoPixel.h>

#define PIN        12   // change to your data pin
#define NUMPIXELS  5    // change to number of LEDs

Adafruit_NeoPixel pixels(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

void setup() {
  pixels.begin();
  pixels.clear();

  // Turn ON all LEDs (green)
  for (int i = 0; i < NUMPIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(0, 150, 0));
  }
  pixels.show();
}

void loop() {
  // nothing - stays ON
}
