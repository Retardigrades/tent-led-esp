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
#define FRAME_TIME 1000 / FRAME_RATE

// Data of the led
union {
  struct {
    CRGB leds[NUM_LEDS];
  };
  unsigned char buffer[NUM_LEDS * sizeof(CRGB)];
} data;

WiFiUDP udpClient;
WiFiUDP udpServer;
WiFiUDP udpControl;
Syslog syslog(udpClient, CONTROLLER, SYSLOG_PORT, HOSTNAME, APP_NAME, LOG_KERN);

void init_leds() {
  // tell FastLED about the LED strip configuration
  FastLED.addLeds<WS2811_PORTA, STRIPS>(data.leds, LED_PER_STRIP)
      .setCorrection(TypicalLEDStrip);

  // set master brightness control
  FastLED.setBrightness(BRIGHTNESS);

  // We do not use dithering for now.
  FastLED.setDither(0);

  // Set an initial color to the channel for debuggung
  CRGB cl[] = {CRGB::Red, CRGB::Blue, CRGB::Green, CRGB::Yellow, CRGB::Purple};
  for (uint8_t i = 0; i < STRIPS; ++i) {
    for (uint8_t j = 0; j < LED_PER_STRIP; ++j) {
      data.leds[i * LED_PER_STRIP + j] = cl[i];
    }
  }
  FastLED.show();
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

  init_leds();

  udpServer.begin(7000);
  udpControl.begin(7001);

  syslog.log(LOG_INFO, "Starting normal operation");
}

void check_wifi() {
  if (WiFi.status() != WL_CONNECTED) {
    syslog.log(LOG_CRIT, "Lost WIFI connection ... Reboot controller");
    delay(50);

    ESP.restart();
  }
}

void check_control() {
  size_t packetSize = udpControl.parsePacket();
  if (0 != packetSize) {
    unsigned char command = udpControl.read();
    syslog.logf(LOG_INFO, "Control command: %02x.", command);

    switch (command) {
      case 0x01:
        syslog.log(LOG_CRIT, "Reboot controller");
        delay(50);
        ESP.restart();
        break;
      case 0x02:
        if (packetSize > 1) {
          unsigned char brightness = udpControl.read();
          syslog.logf(LOG_INFO, "brightness: %d", brightness);
          FastLED.setBrightness(brightness);
          delay(10);
        }
        break;
    }
  }
}

bool check_server(unsigned long loop_time) {
  size_t packetSize = udpServer.parsePacket();
  if (0 == packetSize) {
    delay(3);
    return false;
  }

  //  syslog.logf(LOG_DEBUG, "Got fist framepart packet at %dms...", loop_time);
  size_t read = 0;
  uint8_t packet_cnt = 0;

  while (read < BUFSIZE) {
    while (packetSize <= 0) {
      packetSize = udpServer.parsePacket();
      if (packetSize == 0) {
        if (millis() - loop_time > (FRAME_TIME / 3)) {
          syslog.logf(LOG_DEBUG, "Did not get %d bytes in time, drop frame...",
                      BUFSIZE - read);
          return false;
        }
        delay(1);
      }
    }
    //    syslog.logf(LOG_DEBUG, "Process framepart packet %d with %d bytes at
    //    %d ms...", ++packet_cnt, packetSize, millis());

    size_t current = udpServer.read(data.buffer + read, BUFSIZE - read);
    packetSize -= current;
    read += current;
  }

  //  syslog.logf(LOG_DEBUG, "Write frame to LED at %d ms...", millis());
  FastLED.show();
  //  syslog.logf(LOG_DEBUG, "Frame to LED at %d ms...", millis());
  return true;
}

struct {
  unsigned long last_frame;
  unsigned long last_packet_check;
  unsigned long last_control_check;
} ts;

void loop() {
  unsigned long now = millis();

  if (now - ts.last_control_check > 50) {
    check_wifi();
    check_control();
    ts.last_control_check = now;
  }
  if (now - ts.last_frame < FRAME_TIME / 2) {
    return;
  }
  if (now - ts.last_packet_check < FRAME_TIME / 10) {
    return;
  }
  if (check_server(now)) {
    ts.last_frame = now;
  }
  ts.last_packet_check = now;
}
