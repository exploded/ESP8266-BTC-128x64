# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Flash

This is a PlatformIO project targeting ESP8266 (esp12e). Use the PlatformIO CLI:

```bash
pio run                  # compile
pio run -t upload        # compile and flash
pio device monitor       # serial monitor at 115200 baud
```

## Secrets

WiFi credentials and the weather API URL (contains GPS coordinates) live in `src/secrets.h`, which is **gitignored**. Copy `src/secrets.h.example` to `src/secrets.h` and fill in real values before building.

Never add location data, WiFi credentials, or other personal information to tracked files.

## Architecture

Single-file firmware (`src/main.cpp`) for a BTC price ticker on a 128×64 SSD1306 OLED (HW364 board, yellow/blue bicolour split).

**Display layout:**
- Yellow band (rows 0–15): title/label text
- Blue band (rows 16–63): large value display
- Software I2C on GPIO12 (SCL) and GPIO14 (SDA)

**Display modes** cycle every 5 seconds: BTC/AUD → BTC/USD → Temperature → Time (AEST/AEDT).

**Data flow:** `loop()` refreshes all data every 60s via HTTPS (BearSSL, certificate validation disabled). Time syncs from NTP. All fetches use a shared `fetchJson()` helper. Display redraws every 200ms.

**Key dependencies:** U8g2 (OLED driver), ArduinoJson (API response parsing).
