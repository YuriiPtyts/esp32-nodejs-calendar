# ESP32 calendar display

Node.js runs on your Mac, Raspberry Pi, or another home server. The ESP32 runs
Arduino C++ firmware; it does not run this Node.js application.

```text
Google Calendar → Node.js /next → Wi-Fi → ESP32 → SSD1306
```

## 1. Start the home server

Install dependencies and create your local configuration from the project root:

```sh
npm ci
cp .env.example .env
cp esp32/calendar_display/secrets.example.h esp32/calendar_display/secrets.h
```

Skip the copy commands if you already have your local configuration files.
Fill in `.env` with your Google OAuth credentials. In Google Cloud, enable
the Google Calendar API, create a Desktop app OAuth client, and add yourself
as a test user if the app is in Testing. To get the refresh token, run
`node app/authorize.mjs`, open the printed URL on the same computer, and
approve read-only calendar access. Add the printed refresh token to `.env`.
External apps in Testing normally receive Calendar refresh tokens valid for
seven days. Both `.env` and `secrets.h` are excluded from Git.

Set the address in `app/server.js`'s `app.listen()` call to your server's
actual LAN IP, or use `0.0.0.0` to listen on all local network interfaces.
The current address, `192.168.1.13`, is specific to the original setup.
Then start the server:

```sh
npm start
```

Open `http://YOUR_SERVER_LAN_IP:3000/next` to check the response. It returns
a small JSON object with the event's title, timestamps, local time range,
and current status. This is a Google Calendar integration; it does not fetch
tasks from Notion databases.

The display time uses the server's time zone. Set an IANA time zone in `.env`
if needed, for example `TZ=Europe/Kyiv`.

Find the Mac's Wi-Fi IP address in System Settings → Wi-Fi → Details → TCP/IP.
Check `http://YOUR_MAC_IP:3000/next` from another device on the same network.
Allow incoming Node.js connections if macOS asks. The Mac must stay awake and
the server process must keep running for the display to receive updates.

## 2. Wire the OLED

This sketch assumes a **128×64 I2C SSD1306** and a **classic ESP32 DevKit**.
Other ESP32 variants may need different GPIO pins. A 128×32 or SPI display
needs a different U8g2 constructor and layout.

| SSD1306 | ESP32 |
| --- | --- |
| VCC | 3V3 |
| GND | GND |
| SDA | GPIO 21 |
| SCL | GPIO 22 |

The sketch uses U8g2's default SSD1306 address, usually 0x3C. If your module
uses 0x3D, add `display.setI2CAddress(0x3D * 2);` before `display.begin()`.

## 3. Upload the ESP32 firmware

1. Install Arduino IDE and the **esp32 by Espressif Systems** board package.
2. In Library Manager, install **U8g2** and **ArduinoJson version 7**.
3. Open `esp32/calendar_display/calendar_display.ino`.
4. Edit `WIFI_SSID`, `WIFI_PASSWORD`, and `SERVER_URL` in
   `esp32/calendar_display/secrets.h`. This file is excluded from Git.
   Use your server's LAN IP, not `localhost`: on the ESP32, localhost means
   the ESP32 itself. Use a 2.4 GHz Wi-Fi network for a classic ESP32.
5. Select your ESP32 board and USB port, then upload.

The firmware polls every 30 seconds and draws a live countdown between polls.
An inverted NEXT/NOW badge sits beside the countdown, above a large two-line
title and the local time range. Long titles advance through two-line pages
every four seconds, and a slim progress track fills during an active event.
The included fonts support Latin and Cyrillic text, but not emoji.

The server must return numeric `startsAt` and `endsAt` timestamps in Unix
milliseconds, plus `title` and `timeLabel`. The firmware calculates whether
the event is active locally, rather than relying on a cached `current` flag.
NTP sets the ESP32 clock; the server's standard HTTP Date header provides a
fallback if NTP is unavailable. Request failures retain the previous event
and show an OLD badge and a hollow connection indicator.
Open Serial Monitor at 115200 baud to inspect HTTP errors.

Google credentials remain on the server. `/next` is intended for your trusted
home LAN. If the server's IP changes, update `SERVER_URL`; a router DHCP
reservation can give the server a stable IP.
