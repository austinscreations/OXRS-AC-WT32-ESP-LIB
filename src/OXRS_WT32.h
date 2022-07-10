/*
 * OXRS_WT32.h
 */

#ifndef OXRS_WT32_H
#define OXRS_WT32_H

// Macro for converting env vars to strings
#define STRINGIFY(s) STRINGIFY1(s)
#define STRINGIFY1(s) #s

#include <ArduinoJson.h>  // json handler
#include <OXRS_MQTT.h>    // For MQTT
#include <OXRS_API.h>     // For REST API
#include <WiFi.h>         // Required for Ethernet to get MAC
#include <MqttLogger.h>   // For logging

#if defined(USESENSORS)
#include <OXRS_SENSORS.h> // For QWICC I2C sensors
#endif

#if defined(ETHMODE)
#include <Ethernet.h>     // For networking
#define DHCP_TIMEOUT_MS          15000
#define DHCP_RESPONSE_TIMEOUT_MS 4000
#endif

#if defined(WIFIMODE)
#include <WiFiManager.h>  // For WiFi AP config
#endif

// REST API
#define REST_API_PORT            80

#ifndef SERIAL_BAUD_RATE
#define SERIAL_BAUD_RATE         9600
#endif

enum connectionState_t { CONNECTED_NONE, CONNECTED_IP, CONNECTED_MQTT };

class OXRS_WT32 : public Print
// class OXRS_WT32
{
public:
  OXRS_WT32();

  void initialiseSerial();
  void begin(jsonCallback config, jsonCallback command);
  void loop();
  void restartApi();

  // Firmware can define the config/commands it supports - for device discovery and adoption
  void setConfigSchema(JsonVariant json);
  void setCommandSchema(JsonVariant json);

  // Helpers for publishing to stat/ and tele/ topics
  boolean publishStatus(JsonVariant json);
  boolean publishTelemetry(JsonVariant json);

  void getIPAddressTxt(char *buffer);
  void getMACAddressTxt(char *buffer);
  void getMQTTTopicTxt(char *buffer);

  connectionState_t getConnectionState(void);

  // Implement Print.h wrapper
  virtual size_t write(uint8_t);
  using Print::write;

private:
  boolean _isNetworkConnected(void);
};

#endif