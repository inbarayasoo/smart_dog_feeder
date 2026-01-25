#include "PixelManager.h"
#include <Adafruit_NeoPixel.h>
#include <stdint.h>

// ---------- NeoPixel configuration ----------
#define NEOPIXEL_PIN 12
#define NUM_PIXELS   12

// Which pixel shows Wi-Fi status (blue)
#define WIFI_PIXEL_INDEX 0

Adafruit_NeoPixel pixels(NUM_PIXELS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// ---------- Blink control ----------
const unsigned long BLINK_INTERVAL_MS = 300;
unsigned long lastBlinkMillis = 0;
bool ledOn = false;

// ---------- Initialization ----------
void initPixels() {//initiallize the pixel LEDs
  pixels.begin();
  pixels.clear();
  pixels.show();
  lastBlinkMillis = millis();
  ledOn = false;
}

// ---------- Helpers ----------
void neoOff() {// turn off LEDs
  pixels.clear();
  pixels.show();
}

void neoSetColorAll(uint8_t r, uint8_t g, uint8_t b) { //set color of LEDS
  for (int i = 0; i < NUM_PIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(r, g, b));
  }
  pixels.show();
}

void neoEmptyAndNoWifiPattern() { // neopixel both alerts mode
  pixels.clear();
  for (int i = 0; i < NUM_PIXELS; i++) {
    if (i == WIFI_PIXEL_INDEX) {
      pixels.setPixelColor(i, pixels.Color(0, 0, 255));   // Blue (Wi-Fi)
    } else {
      pixels.setPixelColor(i, pixels.Color(255, 0, 0));   // Red (Empty)
    }
  }
  pixels.show();
}

void neoNoWifiOnlyPattern() { //neopixel only wifi alert mode
  pixels.clear();
  pixels.setPixelColor(WIFI_PIXEL_INDEX, pixels.Color(0, 0, 255)); // Blue
  pixels.show();
}

// ---------- Main update function ----------

// updateNeoPixel(containerEmpty, WiFi.status() == WL_CONNECTED);
void updateNeoPixel(bool containerEmpty, bool wifiConnected) { // change the LEDs according to situation
  unsigned long now = millis();
  bool noWifi = !wifiConnected;

  // No alerts → LEDs OFF
  if (!containerEmpty && !noWifi) {
    if (ledOn) {
      ledOn = false;
      neoOff();
    }
    return;
  }

  // Handle blinking
  if (now - lastBlinkMillis >= BLINK_INTERVAL_MS) {
    lastBlinkMillis = now;
    ledOn = !ledOn;

    if (!ledOn) {
      neoOff();
      return;
    }

    // LED ON → choose pattern
    if (containerEmpty && noWifi) {
      neoEmptyAndNoWifiPattern();   // 11 red + 1 blue
    }
    else if (containerEmpty) {
      neoSetColorAll(255, 0, 0);    // all red
    }
    else {
      neoNoWifiOnlyPattern();       // one blue
    }
  }
}
