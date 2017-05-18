#include <ESP8266httpUpdate.h>

#define UPDATE(host, port, endpoint) do { \
  t_httpUpdate_return ret = ESPhttpUpdate.update(host, port, endpoint,  "TENT_VERSION::" __DATE__ "::" __TIME__); \
  switch(ret) { \
    case HTTP_UPDATE_FAILED: \
    break; \
    case HTTP_UPDATE_NO_UPDATES: \
    break; \
    case HTTP_UPDATE_OK: \
    break; \
  }; \
} while (0)
