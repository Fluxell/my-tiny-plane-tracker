#pragma once
#include "storage.h"
#include "flights.h"

// Initialise the TFT display and turn on the backlight.
void initDisplay();

// Allocate a 240×240 RGB565 frame buffer (112 KB) for the map background.
// Returns nullptr if heap is insufficient — caller falls back to dark mode.
uint16_t* allocBackgroundBuffer();

// Fetch the Stadia Maps base tile for the configured location and range, and
// decode it into `buf`. Returns true on success.
// WiFi must already be connected. buf must be 240*240 uint16_t elements.
bool fetchMapBackground(const AppConfig& cfg, uint16_t* buf);

// Fill the screen with a black background and dim range rings.
void drawDarkBackground();

// Render one complete frame: background (map or dark) then all plane markers.
// bgBuf may be nullptr — dark background is used in that case.
void drawPlanes(const AppConfig& cfg, const PlaneState* planes, int count, uint16_t* bgBuf);

// Display a centered status message (used for WiFi/error states).
void showStatus(const char* msg);
