#include "storage.h"
#include "config.h"
#include <Preferences.h>

bool loadConfig(AppConfig& cfg) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);

    bool hasConfig = prefs.isKey("ssid");
    if (hasConfig) {
        prefs.getString("ssid",       cfg.wifiSSID,  sizeof(cfg.wifiSSID));
        prefs.getString("pass",       cfg.wifiPass,  sizeof(cfg.wifiPass));
        cfg.latitude       = prefs.getFloat("lat",       0.0f);
        cfg.longitude      = prefs.getFloat("lon",       0.0f);
        cfg.refreshSeconds  = prefs.getInt("refresh_s",    REFRESH_DEFAULT);
        cfg.rangeMiles      = prefs.getInt("range_mi",     RANGE_DEFAULT);
        cfg.bgMode          = (uint8_t)prefs.getInt("bg_mode", BG_DARK);
        prefs.getString("stadia_key", cfg.stadiaKey, sizeof(cfg.stadiaKey));
        cfg.minRangeMiles   = prefs.getInt("min_range_mi", RANGE_MIN_DEFAULT);
        cfg.maxRangeMiles   = prefs.getInt("max_range_mi", RANGE_MAX_DEFAULT);
        cfg.minPlanesInView = prefs.getInt("min_planes",   PLANES_MIN_DEFAULT);
        cfg.maxPlanesInView = prefs.getInt("max_planes",   PLANES_MAX_DEFAULT);
        cfg.autoZoom        = prefs.getInt("auto_zoom",    AUTOZOOM_DEFAULT) != 0;
        cfg.showCallsign    = prefs.getInt("show_callsign", 1) != 0;
        cfg.showModel       = prefs.getInt("show_model",    0) != 0;
        cfg.modelFormat     = (uint8_t)prefs.getInt("model_fmt", MODEL_FMT_CODE);
    }

    prefs.end();
    return hasConfig;
}

void saveConfig(const AppConfig& cfg) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putString("ssid",       cfg.wifiSSID);
    prefs.putString("pass",       cfg.wifiPass);
    prefs.putFloat("lat",         cfg.latitude);
    prefs.putFloat("lon",         cfg.longitude);
    prefs.putInt("refresh_s",     cfg.refreshSeconds);
    prefs.putInt("range_mi",      cfg.rangeMiles);
    prefs.putInt("bg_mode",       (int)cfg.bgMode);
    prefs.putString("stadia_key", cfg.stadiaKey);
    prefs.putInt("min_range_mi",  cfg.minRangeMiles);
    prefs.putInt("max_range_mi",  cfg.maxRangeMiles);
    prefs.putInt("min_planes",    cfg.minPlanesInView);
    prefs.putInt("max_planes",    cfg.maxPlanesInView);
    prefs.putInt("auto_zoom",     cfg.autoZoom ? 1 : 0);
    prefs.putInt("show_callsign", cfg.showCallsign ? 1 : 0);
    prefs.putInt("show_model",    cfg.showModel ? 1 : 0);
    prefs.putInt("model_fmt",     (int)cfg.modelFormat);
    prefs.end();
}

void clearConfig() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.clear();
    prefs.end();
}
