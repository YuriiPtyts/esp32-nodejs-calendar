#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <time.h>
#include <sys/time.h>
#include <vector>

// Load local Wi-Fi credentials and the server URL, excluded from Git.
#include "secrets.h"

// Assumes a 128x64 I2C SSD1306 and a classic ESP32 DevKit.
const int SDA_PIN = 21;
const int SCL_PIN = 22;
U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);

// Keep the event locally so the clock and display keep updating between polls.
String title = "";
String timeLabel = "";
String connectionMessage = "Connecting to Wi-Fi";
int64_t startsAt = 0;
int64_t endsAt = 0;
bool hasResponse = false;
bool hasEvent = false;
bool serverOnline = false;
std::vector<String> titleLines;
size_t titlePage = 0;
unsigned long pageChangedAt = 0;
unsigned long lastFetch = 0;
unsigned long lastFrame = 0;
bool fetched = false;

// Google timestamps are milliseconds. NTP keeps this clock running locally.
int64_t nowMilliseconds() {
  return static_cast<int64_t>(time(nullptr)) * 1000;
}

bool clockReady() {
  return time(nullptr) >= 1700000000;
}

// Express supplies an HTTP Date header. Use it if NTP is not available yet,
// so a LAN that blocks public NTP can still display accurate countdowns.
void useServerClock(const String& dateHeader) {
  if (clockReady()) return;

  struct tm date = {};
  char month[4] = {};
  int year = 0;
  if (sscanf(dateHeader.c_str(), "%*3s, %d %3s %d %d:%d:%d GMT",
             &date.tm_mday, month, &year, &date.tm_hour,
             &date.tm_min, &date.tm_sec) != 6) return;

  const char* months = "JanFebMarAprMayJunJulAugSepOctNovDec";
  const char* match = strstr(months, month);
  if (strlen(month) != 3 || !match || year < 2024) return;

  date.tm_mon = (match - months) / 3;
  date.tm_year = year - 1900;
  struct timeval value = {mktime(&date), 0};
  settimeofday(&value, nullptr);
}

// Wrap at word boundaries, preserving UTF-8 characters such as Cyrillic.
// Very long titles get multiple two-line pages rather than tiny text.
void wrapTitle() {
  titleLines.clear();
  titlePage = 0;
  pageChangedAt = millis();
  display.setFont(u8g2_font_9x15_t_cyrillic);

  String remaining = title;
  remaining.replace("\n", " ");
  remaining.replace("\r", " ");
  remaining.replace("\t", " ");
  remaining.trim();
  if (remaining.isEmpty()) remaining = "Untitled";

  while (!remaining.isEmpty()) {
    unsigned int fit = 0;
    int lastSpace = -1;
    while (fit < remaining.length()) {
      unsigned int next = fit + 1;
      while (next < remaining.length() &&
             (static_cast<uint8_t>(remaining[next]) & 0xC0) == 0x80) next++;
      if (display.getUTF8Width(remaining.substring(0, next).c_str()) > 124) break;
      if (remaining[fit] == ' ') lastSpace = fit;
      fit = next;
    }

    // Ensure even an unusually wide glyph cannot stall wrapping.
    if (fit == 0) {
      fit = 1;
      while (fit < remaining.length() &&
             (static_cast<uint8_t>(remaining[fit]) & 0xC0) == 0x80) fit++;
    }

    unsigned int split = fit;
    if (fit < remaining.length() && lastSpace > 0) split = lastSpace;
    String line = remaining.substring(0, split);
    line.trim();
    titleLines.push_back(line);
    remaining = remaining.substring(split);
    remaining.trim();
  }
}

String formatDuration(int64_t milliseconds) {
  if (milliseconds < 60000) return "<1 MIN";
  const int64_t minutes = (milliseconds + 59999) / 60000;
  if (minutes < 60) return String(static_cast<long>(minutes)) + " MIN";
  if (minutes < 1440) {
    return String(static_cast<long>(minutes / 60)) + "H " +
           String(static_cast<long>(minutes % 60)) + "M";
  }
  return String(static_cast<long>(minutes / 1440)) + "D " +
         String(static_cast<long>((minutes % 1440) / 60)) + "H";
}

// Fetch only display data. Google credentials stay on the Node.js server.
void fetchNextEvent() {
  serverOnline = false;
  if (WiFi.status() != WL_CONNECTED) {
    connectionMessage = "Wi-Fi disconnected";
    return;
  }

  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(5000);
  if (!http.begin(SERVER_URL)) {
    connectionMessage = "Server unavailable";
    return;
  }

  const char* headers[] = {"Date"};
  http.collectHeaders(headers, 1);

  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    Serial.printf("HTTP request failed: %d\n", status);
    connectionMessage = "Server unavailable";
    http.end();
    return;
  }

  JsonDocument data; // ArduinoJson version 7.
  const DeserializationError error = deserializeJson(data, http.getString());
  const String serverDate = http.header("Date");
  http.end();

  if (error) {
    connectionMessage = "Invalid server JSON";
    return;
  }

  // A null response means an empty calendar, not a network failure.
  if (data.isNull()) {
    hasEvent = false;
    hasResponse = true;
    serverOnline = true;
    timeLabel = "";
    useServerClock(serverDate);
    return;
  }

  // The server sends 64-bit Unix millisecond timestamps, not ISO strings.
  if (!data.is<JsonObject>() || !data["startsAt"].is<int64_t>() ||
      !data["endsAt"].is<int64_t>() || !data["title"].is<const char*>()) {
    connectionMessage = "Invalid event data";
    return;
  }
  const int64_t newStart = data["startsAt"].as<int64_t>();
  const int64_t newEnd = data["endsAt"].as<int64_t>();
  if (newStart <= 0 || newEnd <= newStart) {
    connectionMessage = "Invalid event times";
    return;
  }

  const String newTitle = data["title"].as<String>();
  if (newTitle != title || !hasEvent) {
    title = newTitle;
    wrapTitle();
  }
  startsAt = newStart;
  endsAt = newEnd;
  timeLabel = data["timeLabel"] | "";
  hasEvent = true;
  hasResponse = true;
  serverOnline = true;
  useServerClock(serverDate);
}

// An inverted status badge and right-aligned countdown form the header.
void drawHeader(const char* badge, const String& rightText) {
  display.setFont(u8g2_font_6x10_tf);
  const int badgeWidth = display.getStrWidth(badge) + 6;
  display.drawRBox(0, 0, badgeWidth, 12, 2);
  display.setDrawColor(0);
  display.drawStr(3, 9, badge);
  display.setDrawColor(1);
  display.drawUTF8(128 - display.getUTF8Width(rightText.c_str()), 9,
                   rightText.c_str());
  display.drawHLine(0, 15, 128);
}

void drawCalendarIcon(int x, int y) {
  display.drawRFrame(x, y + 2, 18, 18, 2);
  display.drawHLine(x, y + 7, 18);
  display.drawVLine(x + 4, y, 5);
  display.drawVLine(x + 13, y, 5);
  display.drawBox(x + 4, y + 10, 3, 3);
  display.drawBox(x + 10, y + 10, 3, 3);
}

// Redraw between polls. Time-based status never depends on stale `current` data.
void drawDisplay() {
  display.clearBuffer();

  if (!hasEvent) {
    const bool empty = hasResponse && serverOnline;
    drawHeader(empty ? "FREE" : "WAIT", empty ? "ALL CLEAR" : "CONNECTING");
    drawCalendarIcon(2, 26);
    display.setFont(u8g2_font_7x13_t_cyrillic);
    display.drawUTF8(28, 34, empty ? "No upcoming" : "Waiting for");
    display.drawUTF8(28, 48, empty ? "events" : "calendar...");
    display.setFont(u8g2_font_5x8_tf);
    display.drawStr(0, 62, empty ? "Enjoy the space." : connectionMessage.c_str());
    display.sendBuffer();
    return;
  }

  const int64_t now = nowMilliseconds();
  const bool synced = clockReady();
  const bool upcoming = synced && now < startsAt;
  const bool active = synced && now >= startsAt && now < endsAt;
  String countdown = "SYNC TIME";
  const char* badge = "NEXT";
  if (upcoming) {
    countdown = "IN " + formatDuration(startsAt - now);
  } else if (active) {
    badge = "NOW";
    countdown = formatDuration(endsAt - now) + " LEFT";
  } else if (synced) {
    badge = "DONE";
    countdown = "REFRESHING";
  }
  if (!serverOnline || WiFi.status() != WL_CONNECTED) badge = "OLD";
  drawHeader(badge, countdown);

  // Show two large, word-wrapped lines, advancing long titles every 4 seconds.
  const size_t pages = (titleLines.size() + 1) / 2;
  if (pages > 1 && millis() - pageChangedAt >= 4000UL) {
    titlePage = (titlePage + 1) % pages;
    pageChangedAt = millis();
  }
  display.setFont(u8g2_font_9x15_t_cyrillic);
  const size_t firstLine = titlePage * 2;
  if (firstLine < titleLines.size())
    display.drawUTF8(2, 31, titleLines[firstLine].c_str());
  if (firstLine + 1 < titleLines.size())
    display.drawUTF8(2, 46, titleLines[firstLine + 1].c_str());

  // Footer: local time range, title-page indicator, and connection indicator.
  display.setFont(u8g2_font_6x10_tf);
  display.drawUTF8(0, 59, timeLabel.c_str());
  if (pages > 1) {
    display.setFont(u8g2_font_4x6_tf);
    const String page = String(static_cast<unsigned int>(titlePage + 1)) +
                        "/" + String(static_cast<unsigned int>(pages));
    display.drawStr(119 - display.getStrWidth(page.c_str()), 59, page.c_str());
  }
  if (serverOnline && WiFi.status() == WL_CONNECTED)
    display.drawDisc(125, 56, 2);
  else
    display.drawCircle(125, 56, 2);

  // A slim progress track fills while the current event is in progress.
  for (int x = 0; x < 128; x += 3) display.drawPixel(x, 63);
  if (active) {
    const int progress = static_cast<int>((now - startsAt) * 128 / (endsAt - startsAt));
    if (progress > 0) display.drawHLine(0, 63, progress);
  }
  display.sendBuffer();
}

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);
  display.begin();
  drawDisplay();

  // UTC is used internally; the server already formats the local time label.
  setenv("TZ", "UTC0", 1);
  tzset();
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  configTime(0, 0, "pool.ntp.org", "time.google.com");
}

void loop() {
  // Poll every 30 seconds. The first request starts as soon as Wi-Fi connects.
  if (WiFi.status() != WL_CONNECTED) {
    connectionMessage = "Wi-Fi disconnected";
    fetched = false;
  } else if (!fetched || millis() - lastFetch >= 30000UL) {
    fetchNextEvent();
    lastFetch = millis();
    fetched = true;
  }

  if (millis() - lastFrame >= 100UL) {
    drawDisplay();
    lastFrame = millis();
  }
  delay(1);
}
