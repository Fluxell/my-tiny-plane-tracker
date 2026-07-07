#pragma once
#include <Arduino.h>

struct AppConfig {
    char    wifiSSID[64];
    char    wifiPass[64];
    float   latitude;
    float   longitude;
    int     refreshSeconds;   // poll interval: 5–120 s, default 15
    int     rangeMiles;       // current display radius; auto-zoom adjusts this at runtime
    uint8_t bgMode;           // BG_DARK or BG_MAP
    char    stadiaKey[64];    // Stadia Maps API key (required when bgMode == BG_MAP)
    int     minRangeMiles;    // auto-zoom lower bound, default 2
    int     maxRangeMiles;    // auto-zoom upper bound, default 60
    int     minPlanesInView;  // expand range when fewer planes visible, default 1
    int     maxPlanesInView;  // shrink range when more planes visible, default 20
    bool    autoZoom;         // enable dynamic range adjustment, default true
};

// Returns true if valid config was found in NVS
bool loadConfig(AppConfig& cfg);
void saveConfig(const AppConfig& cfg);
void clearConfig();
