#include "flights.h"
#include "config.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Static receive buffer — avoids heap fragmentation; large enough for ~80 aircraft
// at 78 nm before we hit MAX_PLANES=30. IncompleteInput on a full buffer is tolerated.
static const size_t JSON_BUF_SIZE = 32768;
static char s_jsonBuf[JSON_BUF_SIZE];

// Range in miles → nautical miles (1 mi = 0.868976 nm)
static int rangeToNM(int miles) {
    return max(1, (int)(miles * 0.868976f + 0.5f));
}

int fetchFlights(const AppConfig& cfg, PlaneState* out, int maxCount) {
    String url = String("https://opendata.adsb.fi/api/v3/lat/")
               + String(cfg.latitude,  4)
               + "/lon/" + String(cfg.longitude, 4)
               + "/dist/" + String(rangeToNM(cfg.rangeMiles));

    Serial.printf("[flights] GET %s\n", url.c_str());

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, url);
    http.setTimeout(HTTP_TIMEOUT_MS);
    // Request uncompressed — ESP32 HTTPClient cannot decompress gzip
    http.addHeader("Accept-Encoding", "identity");

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[flights] HTTP %d\n", code);
        http.end();
        return 0;
    }

    // Buffer the response manually — WiFiClientSecure::read() returns -1 when its
    // internal TLS buffer is momentarily empty, which ArduinoJson misreads as
    // end-of-stream. This loop waits for data the same way httpGetBinary() does.
    auto*         stream     = http.getStreamPtr();
    int           received   = 0;
    int           contentLen = http.getSize();
    unsigned long deadline   = millis() + HTTP_TIMEOUT_MS;

    while ((http.connected() || stream->available()) && millis() < deadline) {
        int avail = stream->available();
        if (avail > 0) {
            int toRead = min(avail, (int)(JSON_BUF_SIZE - 1 - received));
            if (toRead <= 0) break;  // buffer full — parse what we have
            received += stream->readBytes((uint8_t*)s_jsonBuf + received, toRead);
            deadline  = millis() + 5000;
        }
        if (contentLen > 0 && received >= contentLen) break;
        delay(1);
    }
    s_jsonBuf[received] = '\0';
    http.end();

    Serial.printf("[flights] received %d bytes\n", received);

    // Filter to only the fields we need.
    // "ac" is an ARRAY in the response, so the filter must mirror that structure:
    // a nested array containing one template object. Using createNestedObject("ac")
    // instead treats it as an object, causing ArduinoJson to discard every element.
    // ArduinoJson v6: use DynamicJsonDocument. v7: replace with JsonDocument.
    StaticJsonDocument<200> filter;
    JsonObject f = filter.createNestedArray("ac").createNestedObject();
    f["flight"] = true;
    f["t"]      = true;
    f["lat"]    = true;
    f["lon"]    = true;
    f["track"]  = true;

    DynamicJsonDocument doc(24576);
    DeserializationError err = deserializeJson(doc, s_jsonBuf, DeserializationOption::Filter(filter));

    // IncompleteInput means the buffer filled before the JSON ended — acceptable,
    // since we cap at MAX_PLANES anyway and earlier aircraft are fully parsed.
    if (err && err.code() != DeserializationError::IncompleteInput) {
        Serial.printf("[flights] JSON parse failed: %s\n", err.c_str());
        return 0;
    }
    if (err) Serial.printf("[flights] truncated response (%s) — using partial data\n", err.c_str());

    JsonArray ac = doc["ac"];
    int count = 0;
    for (JsonObject p : ac) {
        if (count >= maxCount) break;

        const char* cs = p["flight"] | "";
        strncpy(out[count].callsign, cs, 7);
        out[count].callsign[7] = '\0';

        // Strip trailing spaces
        for (int i = (int)strlen(out[count].callsign) - 1;
             i >= 0 && out[count].callsign[i] == ' '; i--) {
            out[count].callsign[i] = '\0';
        }

        // Skip entries with no callsign
        if (out[count].callsign[0] == '\0') continue;

        const char* t = p["t"] | "";
        strncpy(out[count].typeCode, t, 7);
        out[count].typeCode[7] = '\0';

        out[count].lat   = p["lat"]   | 0.0f;
        out[count].lon   = p["lon"]   | 0.0f;
        out[count].track = p["track"] | 0.0f;
        count++;
    }

    Serial.printf("[flights] %d aircraft in range\n", count);
    return count;
}
