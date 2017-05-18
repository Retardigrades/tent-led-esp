#include <Arduino.h>

#define FASTLED_ESP8266_D1_PIN_ORDER
#define FASTLED_ALLOW_INTERRUPTS 0
//#define FASTLED_INTERRUPT_RETRY_COUNT 0
//#define INTERRUPT_THRESHOLD 1

#include <ESP8266WiFi.h>
#include <SPI.h>
#include <WiFiUdp.h>

#include <FastLED.h>

#include "update.h"

FASTLED_USING_NAMESPACE

#define LED_TYPE WS2812B
#define COLOR_ORDER GRB
#define STRIPS 5
#define LED_PER_STRIP 20
#define NUM_LEDS STRIPS *LED_PER_STRIP
CRGB leds[NUM_LEDS];

#define BRIGHTNESS 96
#define FRAMES_PER_SECOND 50

void setup() {
  // disable sleep mode for better data rate
  WiFi.setSleepMode(WIFI_NONE_SLEEP);

  WiFi.begin("xxx", "yyy");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  UPDATE("brettchen", 6655, "/led_fw");

  // tell FastLED about the LED strip configuration
  FastLED.addLeds<WS2811_PORTA, 5>(leds, 20).setCorrection(TypicalLEDStrip);

  // set master brightness control
  FastLED.setBrightness(BRIGHTNESS);

  CRGB cl[] = {CRGB::Red, CRGB::Blue, CRGB::Green, CRGB::Yellow, CRGB::Purple};
  for (uint8_t i = 0; i < STRIPS; ++i) {
    for (uint8_t j = 0; j < LED_PER_STRIP; ++j) {
      leds[i * LED_PER_STRIP + j] = cl[i];
    }
  }
}

void loop() {
  WiFi.setSleepMode(WIFI_NONE_SLEEP);

  // insert a delay to keep the framerate modest
  FastLED.delay(1000 / FRAMES_PER_SECOND);

  FastLED.show();
}
