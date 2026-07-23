#pragma once
#include <string.h>

// ICAO type designator -> human-readable name, for MODEL_FMT_NAME.
// Flash-resident `const` data — ESP32 reads this directly, no PROGMEM/RAM
// cost. Covers common commercial/regional types; anything not listed here
// falls back to showing the raw code (see lookupTypeName()'s caller).
struct TypeNameEntry { const char* code; const char* name; };

static const TypeNameEntry TYPE_NAMES[] = {
    // Airbus
    {"A319", "Airbus A319"},
    {"A320", "Airbus A320"},
    {"A321", "Airbus A321"},
    {"A20N", "Airbus A320neo"},
    {"A21N", "Airbus A321neo"},
    {"A318", "Airbus A318"},
    {"A332", "Airbus A330-200"},
    {"A333", "Airbus A330-300"},
    {"A339", "Airbus A330-900neo"},
    {"A342", "Airbus A340-200"},
    {"A343", "Airbus A340-300"},
    {"A345", "Airbus A340-500"},
    {"A346", "Airbus A340-600"},
    {"A359", "Airbus A350-900"},
    {"A35K", "Airbus A350-1000"},
    {"A388", "Airbus A380-800"},

    // Boeing
    {"B737", "Boeing 737"},
    {"B738", "Boeing 737-800"},
    {"B739", "Boeing 737-900"},
    {"B37M", "Boeing 737 MAX 7"},
    {"B38M", "Boeing 737 MAX 8"},
    {"B39M", "Boeing 737 MAX 9"},
    {"B734", "Boeing 737-400"},
    {"B735", "Boeing 737-500"},
    {"B744", "Boeing 747-400"},
    {"B748", "Boeing 747-8"},
    {"B752", "Boeing 757-200"},
    {"B753", "Boeing 757-300"},
    {"B762", "Boeing 767-200"},
    {"B763", "Boeing 767-300"},
    {"B764", "Boeing 767-400"},
    {"B772", "Boeing 777-200"},
    {"B77L", "Boeing 777-200LR"},
    {"B773", "Boeing 777-300"},
    {"B77W", "Boeing 777-300ER"},
    {"B788", "Boeing 787-8"},
    {"B789", "Boeing 787-9"},
    {"B78X", "Boeing 787-10"},

    // Bombardier / regional
    {"CRJ2", "Bombardier CRJ200"},
    {"CRJ7", "Bombardier CRJ700"},
    {"CRJ9", "Bombardier CRJ900"},
    {"CRJX", "Bombardier CRJ1000"},
    {"DH8A", "Bombardier Dash 8-100"},
    {"DH8B", "Bombardier Dash 8-200"},
    {"DH8C", "Bombardier Dash 8-300"},
    {"DH8D", "Bombardier Dash 8-400"},

    // Embraer
    {"E135", "Embraer ERJ135"},
    {"E145", "Embraer ERJ145"},
    {"E170", "Embraer E170"},
    {"E175", "Embraer E175"},
    {"E190", "Embraer E190"},
    {"E195", "Embraer E195"},
    {"E75L", "Embraer E175"},
    {"E290", "Embraer E190-E2"},
    {"E295", "Embraer E195-E2"},

    // Other common types
    {"AT72", "ATR 72"},
    {"AT76", "ATR 72-600"},
    {"AT45", "ATR 42"},
    {"C172", "Cessna 172"},
    {"C208", "Cessna Caravan"},
    {"PC12", "Pilatus PC-12"},
    {"GLF5", "Gulfstream G550"},
    {"GLF6", "Gulfstream G650"},
    {"CL60", "Bombardier Challenger 600"},
    {"F900", "Dassault Falcon 900"},
    {"MD11", "McDonnell Douglas MD-11"},
    {"MD80", "McDonnell Douglas MD-80"},
    {"MD82", "McDonnell Douglas MD-82"},
    {"MD83", "McDonnell Douglas MD-83"},
};

// Returns the human-readable name for an ICAO type code, or nullptr if not
// in the table (caller should fall back to showing the raw code).
inline const char* lookupTypeName(const char* code) {
    for (size_t i = 0; i < sizeof(TYPE_NAMES) / sizeof(TYPE_NAMES[0]); i++)
        if (strcmp(TYPE_NAMES[i].code, code) == 0) return TYPE_NAMES[i].name;
    return nullptr;
}
