#include <Arduino.h>

#define FASTLED_ESP8266_D1_PIN_ORDER
#define FASTLED_ALLOW_INTERRUPTS 0

#include <ESP8266WiFi.h>
#include <SPI.h>
#include <WiFiUdp.h>

#include <FastLED.h>
#include <Syslog.h>

#include "config.h"
#include "update.h"

FASTLED_USING_NAMESPACE

#define LED_TYPE WS2812B
#define COLOR_ORDER GRB
#define NUM_LEDS STRIPS *LED_PER_STRIP
#define BUFSIZE NUM_LEDS * sizeof(CRGB)

// Data of the led
union {
  struct {
    CRGB leds[NUM_LEDS];
  };
  unsigned char buffer[NUM_LEDS * sizeof(CRGB)];
} data;

WiFiUDP udpClient;
WiFiUDP udpServer;
Syslog syslog(udpClient, CONTROLLER, SYSLOG_PORT, HOSTNAME, APP_NAME, LOG_KERN);

void init_leds() {
  CRGB cl[] = {CRGB::Red, CRGB::Blue, CRGB::Green, CRGB::Yellow, CRGB::Purple};
  for (uint8_t i = 0; i < STRIPS; ++i) {
    for (uint8_t j = 0; j < LED_PER_STRIP; ++j) {
      data.leds[i * LED_PER_STRIP + j] = cl[i];
    }
  }
}

void setup() {
  // disable sleep mode for better data rate
  WiFi.setSleepMode(WIFI_NONE_SLEEP);

  WiFi.begin(WIFI_SSID, WIFI_PASSWD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  syslog.log(LOG_INFO, "Controller was (re)booted - check for updates");

  UPDATE(CONTROLLER, UPDATE_PORT, UPDATE_EP);

  syslog.log(LOG_INFO, "No updates to handle. init...");

  // tell FastLED about the LED strip configuration
  FastLED.addLeds<WS2811_PORTA, STRIPS>(data.leds, LED_PER_STRIP)
      .setCorrection(TypicalLEDStrip);

  // set master brightness control
  FastLED.setBrightness(BRIGHTNESS);

  init_leds();

  udpServer.begin(7000);

  syslog.log(LOG_INFO, "Starting normal operation");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    ESP.restart();
  }

  size_t packetSize = udpServer.parsePacket();
  if (0 == packetSize) {
    // Use fastled delay for dithering
    FastLED.delay(5);
    return;
  }

  //  syslog.logf(LOG_DEBUG, "Got packet with size %d...", packetSize);

  unsigned char command = udpServer.read();

  switch (command) {
  case 0x01: {
    packetSize -= 1;
    size_t read = 0;
    while (read < BUFSIZE) {
      while (packetSize <= 0) {
        packetSize = udpServer.parsePacket();
        if (packetSize == 0)
          delay(1);
      }

      size_t current = udpServer.read(data.buffer + read, BUFSIZE - read);
      packetSize -= current;
      read += current;
    }
    break;
  }
  case 0x02:
    syslog.log(LOG_CRIT, "Reboot controller");
    delay(50);
    ESP.restart();
    break;
  case 0x03:
    if (packetSize > 1) {
      unsigned char brightness = udpServer.read();
      syslog.logf(LOG_INFO, "brightness: %d", brightness);
      FastLED.setBrightness(brightness);
    }
    break;
  }

  FastLED.show();
  FastLED.delay(1000 / 80);
}
