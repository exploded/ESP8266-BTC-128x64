# ESP8266 BTC Ticker

Bitcoin price ticker for ESP8266 with a 128×64 SSD1306 OLED display (yellow/blue bicolour).
You can buy these on AliExpress for about AUD$6.00 with the screen already conneccted.
[text](README.md)

Cycles through four screens every 5 seconds:
- **BTC/AUD** — current Bitcoin price in Australian dollars
- **BTC/USD** — current Bitcoin price in US dollars
- **Temperature** — local temperature from Open-Meteo
- **Time** — current time (AEST/AEDT via NTP)

Data refreshes every 60 seconds from free public APIs (CoinGecko, Open-Meteo).

## Hardware

- ESP8266 (ESP-12E / NodeMCU)
- 128×64 SSD1306 OLED (HW364, software I2C)
  - SCL → GPIO12 (D6)
  - SDA → GPIO14 (D5)

## Setup

1. Install [PlatformIO](https://platformio.org/)
2. Copy `src/secrets.h.example` to `src/secrets.h` and fill in your WiFi credentials and location coordinates
3. Build and flash:

```bash
pio run -t upload
```

4. Open serial monitor to verify:

```bash
pio device monitor
```
