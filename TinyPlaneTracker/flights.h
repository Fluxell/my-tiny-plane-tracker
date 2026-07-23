#pragma once
#include "storage.h"

struct PlaneState {
    char  callsign[8];   // max 7 chars + null, trailing spaces stripped
    char  typeCode[8];   // ICAO type designator, e.g. "B738"; empty if unavailable
    float lat;
    float lon;
    float track;         // degrees, 0 = North, clockwise
};

// Fetch nearby aircraft from ADSB.fi and fill `out` (up to maxCount entries).
// Returns the number of aircraft stored.
int fetchFlights(const AppConfig& cfg, PlaneState* out, int maxCount);
