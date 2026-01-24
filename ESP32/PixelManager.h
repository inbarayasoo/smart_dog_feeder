#ifndef PIXELMANAGER_H
#define PIXELMANAGER_H

#include <stdint.h>

void neoOff();
void neoSetColorAll(uint8_t r, uint8_t g, uint8_t b);
void initPixels();

// ✅ UPDATED: now also takes wifiConnected
void updateNeoPixel(bool containerEmpty, bool wifiConnected);

#endif
