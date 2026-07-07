#pragma once
#include <Arduino.h>

// Returns true if the device is held face-down (Z-axis < -0.7 g).
// Call once on boot before WiFi init to trigger setup mode.
bool checkFaceDown();

// Configure QMI8658 for single- and double-tap detection. Leaves I2C bus open.
void initTapDetection();

// Read STATUS1 once and return the tap type detected this call.
// STATUS1 self-clears on read, so call this exactly once per loop iteration.
#define TAP_NONE    0
#define TAP_SINGLE  1
#define TAP_DOUBLE  2
uint8_t pollTap();
