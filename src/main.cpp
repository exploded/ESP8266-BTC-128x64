#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <time.h>
#include <ctype.h>
#include "secrets.h"

// ── Display (HW364 — 128×64, SSD1306, yellow/blue bicolour) ──────────────────
// Software I2C: SCL=GPIO12 (D6), SDA=GPIO14 (D5).
// Yellow band: rows 0–15  |  Blue band: rows 16–63
#define SCL_PIN 12  // D6
#define SDA_PIN 14  // D5

U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, SCL_PIN, SDA_PIN, U8X8_PIN_NONE);

#define YELLOW_BASELINE  12
#define BLUE_TOP         16
#define BLUE_HEIGHT      48

#define FETCH_INTERVAL_MS    60000UL
#define WIFI_TIMEOUT_MS      10000UL
#define DISPLAY_CYCLE_MS      5000UL

// ── NTP / Time ────────────────────────────────────────────────────────────────
static const char* NTP_SERVER_1 = "pool.ntp.org";
static const char* NTP_SERVER_2 = "time.nist.gov";
static const char* TZ_INFO      = "AEST-10AEDT,M10.1.0/2,M4.1.0/3";

// ── URLs ──────────────────────────────────────────────────────────────────────
static const char* BTC_AUD_URL = "https://api.coingecko.com/api/v3/simple/price?ids=bitcoin&vs_currencies=aud";
static const char* BTC_USD_URL = "https://api.coingecko.com/api/v3/simple/price?ids=bitcoin&vs_currencies=usd";

// ── State ─────────────────────────────────────────────────────────────────────
static double btcAud     = 0;
static double btcUsd     = 0;
static float  tempC      = 0.0f;
static bool   hasAud     = false;
static bool   hasUsd     = false;
static bool   hasTemp    = false;
static bool   hasTime    = false;

static unsigned long lastFetch       = 0;
static unsigned long lastModeSwitch  = 0;
static uint8_t       displayMode     = 0;  // 0=AUD, 1=USD, 2=TEMP, 3=TIME

// ── Helpers ───────────────────────────────────────────────────────────────────

void drawCentred(const char* str, int y) {
    int w = u8g2.getStrWidth(str);
    u8g2.drawStr((128 - w) / 2, y, str);
}

void drawLargeValue(const char* s) {
    u8g2.setFont(u8g2_font_10x20_tf);
    int w = u8g2.getStrWidth(s);
    if (w <= 128) {
        int baseline = BLUE_TOP + (BLUE_HEIGHT + 16) / 2;
        u8g2.drawStr((128 - w) / 2, baseline, s);
        return;
    }
    u8g2.setFont(u8g2_font_6x10_tf);
    w = u8g2.getStrWidth(s);
    int baseline = BLUE_TOP + (BLUE_HEIGHT + 8) / 2;
    u8g2.drawStr((128 - w) / 2, baseline, s);
}

String formatAud(double p) {
    long whole = (long)p;
    String digits = String(whole);
    String out;
    int len = digits.length();
    for (int i = 0; i < len; i++) {
        if (i > 0 && (len - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return "A$" + out;
}

String formatUsd(double p) {
    long whole = (long)p;
    String digits = String(whole);
    String out;
    int len = digits.length();
    for (int i = 0; i < len; i++) {
        if (i > 0 && (len - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return "U$" + out;
}

String formatTemp(float t) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f C", t);
    return String(buf);
}

String formatTime() {
    time_t now = time(nullptr);
    if (now < 1704067200) return String("--:--");
    struct tm t;
    localtime_r(&now, &t);
    int hour12 = t.tm_hour % 12;
    if (hour12 == 0) hour12 = 12;
    char amPm = (t.tm_hour >= 12) ? 'P' : 'A';
    char buf[16];
    snprintf(buf, sizeof(buf), "%d:%02d%c", hour12, t.tm_min, amPm);
    return String(buf);
}

void showMessage(const char* line1, const char* line2 = nullptr) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tf);
    drawCentred(line1, YELLOW_BASELINE);
    if (line2) {
        int baseline = BLUE_TOP + (BLUE_HEIGHT + 8) / 2;
        int w = u8g2.getStrWidth(line2);
        u8g2.drawStr((128 - w) / 2, baseline, line2);
    }
    u8g2.sendBuffer();
}

void updateDisplay() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tf);

    switch (displayMode) {
        case 0:  // BTC/AUD
            u8g2.drawStr(0, YELLOW_BASELINE, "BITCOIN / AUD");
            if (hasAud) {
                drawLargeValue(formatAud(btcAud).c_str());
            } else {
                drawLargeValue("Fetching...");
            }
            break;

        case 1:  // BTC/USD
            u8g2.drawStr(0, YELLOW_BASELINE, "BITCOIN / USD");
            if (hasUsd) {
                drawLargeValue(formatUsd(btcUsd).c_str());
            } else {
                drawLargeValue("Fetching...");
            }
            break;

        case 2:  // Temperature
            u8g2.drawStr(0, YELLOW_BASELINE, "DONVALE TEMP");
            if (hasTemp) {
                drawLargeValue(formatTemp(tempC).c_str());
            } else {
                drawLargeValue("Fetching...");
            }
            break;

        case 3:  // Time
            u8g2.drawStr(0, YELLOW_BASELINE, "TIME (AEST/AEDT)");
            if (hasTime) {
                drawLargeValue(formatTime().c_str());
            } else {
                drawLargeValue("No sync");
            }
            break;
    }

    u8g2.sendBuffer();
}

// ── WiFi ──────────────────────────────────────────────────────────────────────

bool ensureWiFi() {
    if (WiFi.status() == WL_CONNECTED) return true;
    Serial.println("WiFi connecting...");
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > WIFI_TIMEOUT_MS) {
            Serial.println("WiFi timeout");
            return false;
        }
        delay(50);
    }
    Serial.printf("WiFi connected  ch=%d\n", WiFi.channel());
    return true;
}

// ── Fetch ─────────────────────────────────────────────────────────────────────

bool fetchJson(const char* url, String& payload) {
    BearSSL::WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.begin(client, url);
    http.setTimeout(10000);
    http.addHeader("User-Agent", "ESP8266-BTC/1.0");

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("HTTP error %d for %s\n", code, url);
        http.end();
        return false;
    }
    payload = http.getString();
    http.end();
    return true;
}

bool fetchBtcAud() {
    String payload;
    if (!fetchJson(BTC_AUD_URL, payload)) return false;

    JsonDocument doc;
    if (deserializeJson(doc, payload)) return false;

    double price = doc["bitcoin"]["aud"].as<double>();
    if (price <= 0) return false;

    btcAud = price;
    hasAud = true;
    Serial.printf("BTC/AUD: A$%.0f\n", price);
    return true;
}

bool fetchBtcUsd() {
    String payload;
    if (!fetchJson(BTC_USD_URL, payload)) return false;

    JsonDocument doc;
    if (deserializeJson(doc, payload)) return false;

    double price = doc["bitcoin"]["usd"].as<double>();
    if (price <= 0) return false;

    btcUsd = price;
    hasUsd = true;
    Serial.printf("BTC/USD: $%.0f\n", price);
    return true;
}

bool fetchTemp() {
    String payload;
    if (!fetchJson(WEATHER_URL, payload)) return false;

    JsonDocument doc;
    if (deserializeJson(doc, payload)) return false;

    float t = doc["current"]["temperature_2m"].as<float>();
    tempC   = t;
    hasTemp = true;
    Serial.printf("Temp (Donvale): %.1f C\n", t);
    return true;
}

void syncTime() {
    configTzTime(TZ_INFO, NTP_SERVER_1, NTP_SERVER_2);
    unsigned long start = millis();
    while (time(nullptr) < 1704067200 && millis() - start < 5000) {
        delay(100);
    }
    if (time(nullptr) > 1704067200) {
        hasTime = true;
        Serial.println("NTP synced");
    } else {
        Serial.println("NTP sync failed");
    }
}

// ── Entry point ───────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(100);

    u8g2.begin();
    Serial.println("OLED ok");

    showMessage("BITCOIN TICKER", "Connecting...");

    WiFi.mode(WIFI_STA);
    if (!ensureWiFi()) {
        showMessage("WiFi", "failed!");
        return;
    }

    syncTime();
    fetchBtcAud();
    fetchBtcUsd();
    fetchTemp();

    lastFetch      = millis();
    lastModeSwitch = millis();

    updateDisplay();
}

void loop() {
    unsigned long now = millis();

    // Cycle display mode
    if (now - lastModeSwitch >= DISPLAY_CYCLE_MS) {
        displayMode    = (displayMode + 1) % 4;
        lastModeSwitch = now;
        Serial.printf("Mode -> %u\n", displayMode);
    }

    // Refresh data periodically
    if (now - lastFetch >= FETCH_INTERVAL_MS) {
        lastFetch = now;
        if (ensureWiFi()) {
            syncTime();
            fetchBtcAud();
            fetchBtcUsd();
            fetchTemp();
        }
    }

    // Always redraw (keeps time ticking)
    updateDisplay();
    delay(200);
}
