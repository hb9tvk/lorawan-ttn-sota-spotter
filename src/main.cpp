#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <TinyGPS++.h>
#include <TFT_eSPI.h>
#include "Menu.h"
#include "summits.h"
#include "config.h"

#define GPS_BAUD 115200
#define GPS_RX              34
#define GPS_TX              33
#define GPS_PPS             36
#define GPS_RESET           35

#define VEXT_CTRL           3   // To turn on GPS and TFT
#define BOARD_I2C_SDA       7
#define BOARD_I2C_SCL       6

#define BUTTON_PIN  0

HardwareSerial  gpsSerial(1);
TinyGPSPlus gps;

TFT_eSPI tft = TFT_eSPI();

Menu menu = Menu();
String mySummit;
float dist;
loraSpot spot;

bool sendLora(uint8_t *msg, int len) {
  int16_t state = node.sendReceive(msg, len);
  debug(state < RADIOLIB_ERR_NONE, F("Error in sendReceive"), state, false);
  if(state > 0) {
    Serial.println(F("Received a downlink"));
  } else {
    Serial.println(F("No downlink received"));
  }
  return state >= RADIOLIB_ERR_NONE;
}

void statusMessage(int color, String msg) {
    tft.fillScreen(color);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(10,30);
    tft.println(msg);
}

int findNearestSummit(float lat, float lng) {
  float mindist=MAXFLOAT;
  int summit=0;

  int numsum=sizeof(summits)/sizeof(summits[0]);
  for (int i=0;i<numsum;i++) {
    dist=gps.distanceBetween(lat,lng,summits[i].lat,summits[i].lon);
    if (dist<mindist) {
      mindist=dist;
      summit=i;
      Serial.printf("dist=%f summit=%s\n",dist,summits[i].ref);
    }
  }
  dist=mindist;
  return(summit);
}

void setup() {

  Serial.begin(115200);
  //while(!Serial);
  //delay(5000);  // Give time to switch to the serial monitor
  Serial.println(F("\nSetup ... "));
  // switch on power for TFT and GPS  
  pinMode(VEXT_CTRL,OUTPUT);
  digitalWrite(VEXT_CTRL, HIGH);
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_TX, GPS_RX);
  Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);
  SPI.begin(9, 11, 10);

  // setup TFT
  Serial.println("Initializing TFT");
  tft.init();
  tft.begin();
  tft.setTextFont(0);
  tft.setRotation(1);
  digitalWrite(TFT_BL, HIGH);

  statusMessage(TFT_ORANGE, "Init Radio");
  Serial.println(F("Initialise the radio"));
  int16_t state = radio.begin();
  if (state != RADIOLIB_ERR_NONE) {
    debug(true, F("Initialise radio failed"), state, false);
    statusMessage(TFT_RED, "Failed");
    while(1) delay(1000);
  }

  // Setup the OTAA session information
  state = node.beginOTAA(joinEUI, devEUI, nwkKey, appKey);
  if (state != RADIOLIB_ERR_NONE) {
    debug(true, F("Initialise node failed"), state, false);
    statusMessage(TFT_RED, "Failed");
    while(1) delay(1000);
  }

  Serial.println(F("Join ('login') the LoRaWAN Network"));
  const int MAX_JOIN_ATTEMPTS = 10;
  for (int attempt = 1; attempt <= MAX_JOIN_ATTEMPTS; attempt++) {
    tft.fillScreen(TFT_ORANGE);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(10, 15);
    tft.println("Joining");
    tft.setCursor(10, 45);
    tft.println(String(attempt) + "/" + String(MAX_JOIN_ATTEMPTS));
    Serial.printf("Join attempt %d/%d\n", attempt, MAX_JOIN_ATTEMPTS);

    state = node.activateOTAA();
    if (state == RADIOLIB_LORAWAN_NEW_SESSION) break;

    debug(true, F("Join failed"), state, false);
    if (attempt < MAX_JOIN_ATTEMPTS) delay(5000);
  }
  if (state != RADIOLIB_LORAWAN_NEW_SESSION) {
    tft.fillScreen(TFT_RED);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(10, 15);
    tft.println("TTN not");
    tft.setCursor(10, 45);
    tft.println("reachable");
    Serial.println(F("Could not join TTN after max attempts"));
    while(1) delay(1000);
  }

  statusMessage(TFT_GREEN, "Join OK");
  delay(2000);

  unsigned long lastGpsDisplay = 0;
  unsigned long gpsWaitStart = millis();
  while (true) {
    while (gpsSerial.available() > 0) {
      gps.encode(gpsSerial.read());
    }
    unsigned long now = millis();
    if (now - lastGpsDisplay >= 1000) {
      lastGpsDisplay = now;
      unsigned long elapsed = (now - gpsWaitStart) / 1000;
      uint32_t chars = gps.charsProcessed();
      char timebuf[6];
      snprintf(timebuf, sizeof(timebuf), "%02lu:%02lu", elapsed / 60, elapsed % 60);

      tft.fillScreen(TFT_BLUE);
      tft.setTextColor(TFT_WHITE);
      tft.setTextSize(2);
      tft.setCursor(10, 5);
      tft.println("Wait GPS");
      tft.setCursor(10, 30);
      tft.println(timebuf);
      tft.setCursor(10, 55);
      if (chars == 0) {
        tft.print("No data!");
      } else {
        tft.print(chars);
        tft.print(" B");
      }
    }
    if (gps.location.isValid() && gps.location.age() < 2000) break;
  }

  // Drain any remaining GPS sentences so satellite count / HDOP are up to date
  unsigned long settle = millis();
  while (millis() - settle < 1000) {
    while (gpsSerial.available() > 0) gps.encode(gpsSerial.read());
  }

  mySummit = summits[findNearestSummit(gps.location.lat(), gps.location.lng())].ref;

  tft.fillScreen(TFT_GREEN);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 5);
  tft.println("GPS Locked!");
  tft.setCursor(10, 30);
  tft.println(mySummit);
  tft.setCursor(10, 55);
  if (dist < 1000) {
    tft.print((int)dist);
    tft.print("m");
  } else {
    tft.print(dist / 1000.0, 1);
    tft.print("km");
  }
  delay(3000);

  tft.fillScreen(TFT_BLACK);
  menu.init();
  menu.draw();
  Serial.println(F("Ready!\n"));
}

void loop() {
  menu.tickButtons();
  while (gpsSerial.available() > 0) {
    menu.tickButtons();
    gps.encode(gpsSerial.read());
  }

  // if (gps.time.isUpdated()) {
  //   Serial.println("Time updated");
  // }
}