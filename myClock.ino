/*   myClock -- ESP8266 WiFi NTP Clock for pixel displays
     Copyright (c) 2018 David M Denney <dragondaud@gmail.com>
     distributed under the terms of the MIT License
*/

#include <ESP8266WiFi.h>        //https://github.com/esp8266/Arduino
#include <ESP8266HTTPClient.h>
#include <ArduinoOTA.h>
#include <time.h>
#include "FS.h"
#include <ArduinoJson.h>        // https://github.com/bblanchon/ArduinoJson/
#include <WiFiManager.h>        // https://github.com/tzapu/WiFiManager
#include "display.h"

#define APPNAME "myClock"
#define VERSION "0.9.12"

#define SYSLOG
String syslogSrv = "syslog";
uint16_t syslogPort = 514;
String tzKey = "";                // API key from https://timezonedb.com/register
String owKey = "";                // API key from https://home.openweathermap.org/api_keys
String softAPpass = "ConFigMe";   // password for SoftAP config
uint8_t brightness = 255;         // 0-255 display brightness
bool milTime = true;              // set false for 12hour clock
String location = "";             // zipcode or empty for geoIP location
String timezone = "";             // timezone from https://timezonedb.com/time-zones or empty for geoIP
int threshold = 500;

// Syslog
#ifdef SYSLOG
#include <Syslog.h>             // https://github.com/arcao/Syslog
WiFiUDP udpClient;
Syslog syslog(udpClient, SYSLOG_PROTO_IETF);
#endif

ESP8266WebServer server(80);

static const char* UserAgent PROGMEM = "myClock/1.0 (Arduino ESP8266)";

time_t TWOAM, pNow, wDelay;
int pHH, pMM, pSS, light;
long offset;
char HOST[20];
bool saveConfig = false;
uint8_t dim;
bool LIGHT = true;

void setup() {
  Serial.begin(115200);
  while (!Serial);
  Serial.println();
  readSPIFFS();

  display.begin(16);
  display_ticker.attach(0.002, display_updater);
  display.clearDisplay();
  display.setFont(&TomThumb);
  display.setTextWrap(false);
  display.setTextColor(myColor);
  display.setBrightness(brightness);

  drawImage(0, 0); // display splash image while connecting

  startWiFi();
  if (saveConfig) writeSPIFFS();

#ifdef SYSLOG
  syslog.server(syslogSrv.c_str(), syslogPort);
  syslog.deviceHostname(HOST);
  syslog.appName(APPNAME);
  syslog.defaultPriority(LOG_INFO);
#endif

  if (location == "") location = getIPlocation();
  else while (timezone == "") getIPlocation();

  display.clearDisplay();
  display.setCursor(2, row1);
  display.setTextColor(myGREEN);
  display.print(HOST);
  display.setCursor(2, row2);
  display.setTextColor(myBLUE);
  display.print(WiFi.localIP());
  display.setCursor(2, row3);
  display.setTextColor(myMAGENTA);
  display.print(timezone);
  display.setCursor(2, row4);
  display.setTextColor(myCYAN);
  display.print(F("waiting for ntp"));
  light = analogRead(A0);
  Serial.printf("setup: %s, %s, %s, %d, %d \r\n",
                location.c_str(), timezone.c_str(), milTime ? "true" : "false", brightness, light);
#ifdef SYSLOG
  syslog.logf(LOG_INFO, "setup: %s|%s|%s|%d|%d",
              location.c_str(), timezone.c_str(), milTime ? "true" : "false", brightness, light);
#endif
  setNTP(timezone);
  delay(1000);
  startWebServer();
  displayDraw(brightness);
  getWeather();
} // setup

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  time_t now = time(nullptr);
  if (now != pNow) {
    if (now > TWOAM) setNTP(timezone);
    int ss = now % 60;
    int mm = (now / 60) % 60;
    int hh = (now / (60 * 60)) % 24;
    if ((!milTime) && (hh > 12)) hh -= 12;
    if (ss != pSS) {
      int s0 = ss % 10;
      int s1 = ss / 10;
      if (s0 != digit0.Value()) digit0.Morph(s0);
      if (s1 != digit1.Value()) digit1.Morph(s1);
      pSS = ss;
      getLight();
    }

    if (mm != pMM) {
      int m0 = mm % 10;
      int m1 = mm / 10;
      if (m0 != digit2.Value()) digit2.Morph(m0);
      if (m1 != digit3.Value()) digit3.Morph(m1);
      pMM = mm;
      Serial.printf("%02d:%02d %3d %3d \r", hh, mm, light, dim);
    }

    if (hh != pHH) {
      int h0 = hh % 10;
      int h1 = hh / 10;
      if (h0 != digit4.Value()) digit4.Morph(h0);
      if (h1 != digit5.Value()) digit5.Morph(h1);
      pHH = hh;
    }
    pNow = now;
    if (now > wDelay) getWeather();
  }
}

void displayDraw(uint8_t b) {
  display.clearDisplay();
  display.setBrightness(b);
  dim = b;
  time_t now = time(nullptr);
  int ss = now % 60;
  int mm = (now / 60) % 60;
  int hh = (now / (60 * 60)) % 24;
  if ((!milTime) && (hh > 12)) hh -= 12;
  Serial.printf("%02d:%02d\r", hh, mm);
  digit1.DrawColon(myColor);
  digit3.DrawColon(myColor);
  digit0.Draw(ss % 10, myColor);
  digit1.Draw(ss / 10, myColor);
  digit2.Draw(mm % 10, myColor);
  digit3.Draw(mm / 10, myColor);
  digit4.Draw(hh % 10, myColor);
  digit5.Draw(hh / 10, myColor);
  pNow = now;
}

void getLight() {
  if (!LIGHT) return;
  int lt = analogRead(A0);
  if (lt > 20) {
    light = (light * 3 + lt) >> 2;
    if (light >= threshold) dim = brightness;
    else if (light < (threshold >> 3)) dim = brightness >> 4;
    else if (light < (threshold >> 2)) dim = brightness >> 3;
    else if (light < (threshold >> 1)) dim = brightness >> 2;
    else if (light < threshold) dim = brightness >> 1;
    display.setBrightness(dim);
  }
}
