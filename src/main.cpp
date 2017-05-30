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
#define COUNT_FRAMES_TO 10000

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

struct {
  unsigned long last_frame;
  unsigned long last_packet_check;
  unsigned long last_control_check;
} ts;

uint16_t frame_cnt = 0;

void eventWiFi(WiFiEvent_t event) {
  switch (event) {
    case WIFI_EVENT_STAMODE_CONNECTED:
      break;

    case WIFI_EVENT_STAMODE_DISCONNECTED:
      // Maybe find a better solution for this.
      ESP.restart();
      break;

    case WIFI_EVENT_STAMODE_AUTHMODE_CHANGE:
      break;

    case WIFI_EVENT_STAMODE_GOT_IP:
      break;

    case WIFI_EVENT_STAMODE_DHCP_TIMEOUT:
      break;

    case WIFI_EVENT_SOFTAPMODE_STACONNECTED:
      break;

    case WIFI_EVENT_SOFTAPMODE_STADISCONNECTED:
      break;

    case WIFI_EVENT_SOFTAPMODE_PROBEREQRECVED:
      break;
  }
}

void netSetup() {
  WiFi.onEvent(eventWiFi);
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
}

void init_leds() {
  // Set GPIO15 to pullup
  pinMode(5, OUTPUT);
  digitalWrite(5, LOW);

#ifdef DEBUG
  Serial.println("Set LED");
#endif

  // tell FastLED about the LED strip configuration
  FastLED.addLeds<WS2811_PORTA, STRIPS>(data.leds, LED_PER_STRIP)
      .setCorrection(TypicalLEDStrip);

  // set master brightness control
  FastLED.setBrightness(BRIGHTNESS);

  // We do not use dithering for now.
  FastLED.setDither(0);

#ifdef DEBUG
  // Set an initial color to the channel for debuggung
  CRGB cl[] = {CRGB::Red, CRGB::Blue, CRGB::Green, CRGB::Yellow, CRGB::Purple};
  for (uint8_t i = 0; i < STRIPS; ++i) {
    for (uint8_t j = 0; j < LED_PER_STRIP; ++j) {
      data.leds[i * LED_PER_STRIP + j] = cl[i];
    }
  }
  FastLED.show();
#endif
}

void setup() {
#ifdef DEBUG
  Serial.begin(115200);
  Serial.setDebugOutput(true);
#endif

  // disable sleep mode for better data rate
  WiFi.setSleepMode(WIFI_NONE_SLEEP);

  netSetup();

  syslog.log(LOG_INFO, "Controller was (re)booted - check for updates");

  UPDATE(CONTROLLER, UPDATE_PORT, UPDATE_EP);

  init_leds();

  udpServer.begin(7000);
  udpControl.begin(7001);

  syslog.log(LOG_INFO, "Starting normal operation");
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
    // delay(3);
    return false;
  }

  if (FIST_PACKET_SIZE > packetSize) {
    syslog.logf(
        LOG_DEBUG,
        "Drop packet because first packet smaler than %d bytes (was %d)",
        FIST_PACKET_SIZE, packetSize);
    return false;
  }

  size_t read = 0;

  while (read < BUFSIZE) {
    while (packetSize <= 0) {
      packetSize = udpServer.parsePacket();
      if (packetSize == 0) {
        if (millis() - loop_time > (FRAME_TIME / 3)) {
          syslog.logf(LOG_DEBUG,
                      "Did not get %d bytes in "
                      "time, drop frame...",
                      BUFSIZE - read);
          return false;
        }
        delay(1);
      }
    }

    size_t current = udpServer.read(data.buffer + read, BUFSIZE - read);
    packetSize -= current;
    read += current;
  }

  FastLED.show();
  return true;
}


void loop() {
  unsigned long now = millis();

  if (now - ts.last_control_check > 50) {
    check_control();
    ts.last_control_check = now;
  }

  if ((now - ts.last_frame < FRAME_TIME / 2) ||
      (now - ts.last_packet_check < FRAME_TIME / 10)) {
    return;
  }

  if (check_server(now)) {

#ifdef COUNT_FRAMES_TO
    if (0 == (++frame_cnt % COUNT_FRAMES_TO)) {
      syslog.logf(LOG_DEBUG, "%d Frames written to LED", COUNT_FRAMES_TO);
      frame_cnt = 0;
    }
#endif

    ts.last_frame = now;
  }

  ts.last_packet_check = now;
}
