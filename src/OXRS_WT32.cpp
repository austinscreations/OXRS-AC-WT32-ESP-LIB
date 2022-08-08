/*
 * OXRS_WT32.cpp
 */

#include "Arduino.h"
#include <OXRS_WT32.h>

#include <Ethernet.h>     // For networking
#include <WiFi.h>         // Required for Ethernet to get MAC
#include <MqttLogger.h>   // For logging

#if defined(WIFI_MODE)
#include <WiFiManager.h>  // For WiFi AP config
#endif

#if defined(LV_USE_SNAPSHOT)
#include <lvgl.h>         // For graphic snapshots
#endif

// Macro for converting env vars to strings
#define STRINGIFY(s) STRINGIFY1(s)
#define STRINGIFY1(s) #s

// Network client (for MQTT)/server (for REST API)
#if defined(ETH_MODE)
// Ethernet client
EthernetClient _client;
EthernetServer _server(REST_API_PORT);
#else
// Wifi client
WiFiClient _client;
WiFiServer _server(REST_API_PORT);
#endif

// MQTT client
PubSubClient _mqttClient(_client);
OXRS_MQTT _mqtt(_mqttClient);

// REST API
OXRS_API _api(_mqtt);

// I2C sensors
#if defined(I2C_SENSORS)
OXRS_SENSORS _sensors(_mqtt);
#endif

// Logging (topic updated once MQTT connects successfully)
MqttLogger _logger(_mqttClient, "log", MqttLoggerMode::MqttAndSerial);

// Supported firmware config and command schemas
DynamicJsonDocument _fwConfigSchema(4096);
DynamicJsonDocument _fwCommandSchema(2048);

// MQTT callbacks wrapped by _mqttConfig/_mqttCommand
jsonCallback _onConfig;
jsonCallback _onCommand;

/* JSON helpers */
void _mergeJson(JsonVariant dst, JsonVariantConst src)
{
  if (src.is<JsonObject>())
  {
    for (auto kvp : src.as<JsonObjectConst>())
    {
      _mergeJson(dst.getOrAddMember(kvp.key()), kvp.value());
    }
  }
  else
  {
    dst.set(src);
  }
}

/* Adoption info builders */
void _getFirmwareJson(JsonVariant json)
{
  JsonObject firmware = json.createNestedObject("firmware");

  firmware["name"] = FW_NAME;
  firmware["shortName"] = FW_SHORT_NAME;
  firmware["maker"] = FW_MAKER;
  firmware["version"] = STRINGIFY(FW_VERSION);

#if defined(FW_GITHUB_URL)
  firmware["githubUrl"] = FW_GITHUB_URL;
#endif
}

void _getSystemJson(JsonVariant json)
{
  JsonObject system = json.createNestedObject("system");

  system["heapUsedBytes"] = ESP.getHeapSize();
  system["heapFreeBytes"] = ESP.getFreeHeap();
  system["heapMaxAllocBytes"] = ESP.getMaxAllocHeap();
  system["flashChipSizeBytes"] = ESP.getFlashChipSize();

  system["sketchSpaceUsedBytes"] = ESP.getSketchSize();
  system["sketchSpaceTotalBytes"] = ESP.getFreeSketchSpace();

  system["fileSystemUsedBytes"] = SPIFFS.usedBytes();
  system["fileSystemTotalBytes"] = SPIFFS.totalBytes();

  system["availablePsRamBytes"] = ESP.getPsramSize();
  system["freePsRamBytes"] = ESP.getFreePsram();
}

void _getNetworkJson(JsonVariant json)
{
  JsonObject network = json.createNestedObject("network");

  byte mac[6];

#if defined(ETH_MODE)
  Ethernet.MACAddress(mac);
  
  network["mode"] = "ethernet";
  network["ip"] = Ethernet.localIP();
#else
  WiFi.macAddress(mac);

  network["mode"] = "wifi";
  network["ip"] = WiFi.localIP();
#endif

  char mac_display[18];
  sprintf_P(mac_display, PSTR("%02X:%02X:%02X:%02X:%02X:%02X"), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  network["mac"] = mac_display;
}

void _getConfigSchemaJson(JsonVariant json)
{
  JsonObject configSchema = json.createNestedObject("configSchema");

  // Config schema metadata
  configSchema["$schema"] = JSON_SCHEMA_VERSION;
  configSchema["title"] = FW_SHORT_NAME;
  configSchema["type"] = "object";

  JsonObject properties = configSchema.createNestedObject("properties");

  // Firmware config schema (if any)
  if (!_fwConfigSchema.isNull())
  {
    _mergeJson(properties, _fwConfigSchema.as<JsonVariant>());
  }

  // Add any sensor config
#if defined(I2C_SENSORS)
  _sensors.setConfigSchema(properties);
#endif
}

void _getCommandSchemaJson(JsonVariant json)
{
  JsonObject commandSchema = json.createNestedObject("commandSchema");

  // Command schema metadata
  commandSchema["$schema"] = JSON_SCHEMA_VERSION;
  commandSchema["title"] = FW_SHORT_NAME;
  commandSchema["type"] = "object";

  JsonObject properties = commandSchema.createNestedObject("properties");

  // Firmware command schema (if any)
  if (!_fwCommandSchema.isNull())
  {
    _mergeJson(properties, _fwCommandSchema.as<JsonVariant>());
  }

  // Add any sensor commands
#if defined(I2C_SENSORS)
  _sensors.setCommandSchema(properties);
#endif
}

/* API callbacks */
void _apiAdopt(JsonVariant json)
{
  // Build device adoption info
  _getFirmwareJson(json);
  _getSystemJson(json);
  _getNetworkJson(json);
  _getConfigSchemaJson(json);
  _getCommandSchemaJson(json);
}

#if defined(LV_USE_SNAPSHOT)
// Snapshot API
void _getApiSnapshot(Request &req, Response &res)
{
  // Take a snapshot
  lv_img_dsc_t *snapshot = lv_snapshot_take(lv_scr_act(), LV_IMG_CF_TRUE_COLOR_ALPHA);

  uint8_t * bufferPtr = (uint8_t *)snapshot->data;
  size_t bufferSize = WT32_SCREEN_WIDTH * WT32_SCREEN_HEIGHT * 3;

  // Convert to RGB888
  lv_color_t color;
  for (int i = 0; i < bufferSize; i += 3)
  {
    color.full = bufferPtr[i] + bufferPtr[i + 1] * 0x100;
    bufferPtr[i]      = (color.ch.blue * 255) / 31;
    bufferPtr[i + 1]  = (color.ch.green * 255) / 63;
    bufferPtr[i + 2]  = (color.ch.red * 255) / 31;
  }

  // build header for .bmp file
  struct bmpHeader_t
  {
    uint8_t  magic [2]        = {'B', 'M'};
    uint32_t bfSize           = (uint32_t)(WT32_SCREEN_WIDTH * WT32_SCREEN_HEIGHT * 3);
    uint32_t bfReserved       = 0;
    uint32_t bfOffBits        = sizeof(bmpHeader_t);

    uint32_t biSize           = 40;
    int32_t biWidth           = WT32_SCREEN_WIDTH;
    int32_t biHeight          = -WT32_SCREEN_HEIGHT;
    uint16_t biPlanes         = 1;
    uint16_t biBitCount       = 24;
    uint32_t biCompression    = 0;
    uint32_t biSizeImage      = bfSize;
    int32_t biXPelsPerMeter   = 2836;
    int32_t biYPelsPerMeter   = 2836;
    uint32_t biClrUsed        = 0;
    uint32_t biClrImportant   = 0;

    uint32_t bdMask[3]        = {0x0, 0x0, 0x0};
  } __attribute__((packed)) bmpHeader;

  // Return the snapshot image to the caller
  res.set("Content-Type", "image/bmp");
  // supporting both types of write increase connectivety / download speeds
  // ethernet only works on writeP due to buffer limits
  #if defined(ETH_MODE)
    res.writeP((uint8_t *)&bmpHeader, sizeof(bmpHeader));
    res.writeP(bufferPtr, bufferSize);
  #elif defined(WIFI_MODE)
    res.write((uint8_t *)&bmpHeader, sizeof(bmpHeader));
    res.write(bufferPtr, bufferSize);
  #endif

  // free used memory
  lv_snapshot_free(snapshot);
}
#endif

/* MQTT callbacks */
void _mqttConnected()
{
  // MqttLogger doesn't copy the logging topic to an internal
  // buffer so we have to use a static array here
  static char logTopic[64];
  _logger.setTopic(_mqtt.getLogTopic(logTopic));

  // Publish device adoption info
  DynamicJsonDocument json(JSON_ADOPT_MAX_SIZE);
  _mqtt.publishAdopt(_api.getAdopt(json.as<JsonVariant>()));

  // Log the fact we are now connected
  _logger.println("[wt32] mqtt connected");
}

void _mqttDisconnected(int state)
{
  // Log the disconnect reason
  // See https://github.com/knolleary/pubsubclient/blob/2d228f2f862a95846c65a8518c79f48dfc8f188c/src/PubSubClient.h#L44
  switch (state)
  {
  case MQTT_CONNECTION_TIMEOUT:
    _logger.println(F("[wt32] mqtt connection timeout"));
    break;
  case MQTT_CONNECTION_LOST:
    _logger.println(F("[wt32] mqtt connection lost"));
    break;
  case MQTT_CONNECT_FAILED:
    _logger.println(F("[wt32] mqtt connect failed"));
    break;
  case MQTT_DISCONNECTED:
    _logger.println(F("[wt32] mqtt disconnected"));
    break;
  case MQTT_CONNECT_BAD_PROTOCOL:
    _logger.println(F("[wt32] mqtt bad protocol"));
    break;
  case MQTT_CONNECT_BAD_CLIENT_ID:
    _logger.println(F("[wt32] mqtt bad client id"));
    break;
  case MQTT_CONNECT_UNAVAILABLE:
    _logger.println(F("[wt32] mqtt unavailable"));
    break;
  case MQTT_CONNECT_BAD_CREDENTIALS:
    _logger.println(F("[wt32] mqtt bad credentials"));
    break;
  case MQTT_CONNECT_UNAUTHORIZED:
    _logger.println(F("[wt32] mqtt unauthorised"));
    break;
  }
}

void _mqttConfig(JsonVariant json)
{
  // Pass on to the firmware callback
  if (_onConfig)
  {
    _onConfig(json);
  }

  // Let the sensors handle any config
#if defined(I2C_SENSORS)
  _sensors.conf(json);
#endif
}

void _mqttCommand(JsonVariant json)
{
  // Pass on to the firmware callback
  if (_onCommand)
  {
    _onCommand(json);
  }

  // Let the sensors handle any commands
#if defined(I2C_SENSORS)
  _sensors.cmnd(json);
#endif
}

void _mqttCallback(char *topic, byte *payload, int length)
{
  // Pass down to our MQTT handler and check it was processed ok
  int state = _mqtt.receive(topic, payload, length);
  switch (state)
  {
  case MQTT_RECEIVE_ZERO_LENGTH:
    _logger.println(F("[wt32] empty mqtt payload received"));
    break;
  case MQTT_RECEIVE_JSON_ERROR:
    _logger.println(F("[wt32] failed to deserialise mqtt json payload"));
    break;
  case MQTT_RECEIVE_NO_CONFIG_HANDLER:
    _logger.println(F("[wt32] no mqtt config handler"));
    break;
  case MQTT_RECEIVE_NO_COMMAND_HANDLER:
    _logger.println(F("[wt32] no mqtt command handler"));
    break;
  }
}

/* Main program */
void OXRS_WT32::setMqttBroker(const char *broker, uint16_t port)
{
  _mqtt.setBroker(broker, port);
}

void OXRS_WT32::setMqttClientId(const char *clientId)
{
  _mqtt.setClientId(clientId);
}

void OXRS_WT32::setMqttAuth(const char *username, const char *password)
{
  _mqtt.setAuth(username, password);
}

void OXRS_WT32::setMqttTopicPrefix(const char *prefix)
{
  _mqtt.setTopicPrefix(prefix);
}

void OXRS_WT32::setMqttTopicSuffix(const char *suffix)
{
  _mqtt.setTopicSuffix(suffix);
}

void OXRS_WT32::begin(jsonCallback config, jsonCallback command)
{
  // Get our firmware details
  DynamicJsonDocument json(128);
  _getFirmwareJson(json.as<JsonVariant>());

  // Log firmware details
  _logger.print(F("[wt32] "));
  serializeJson(json, _logger);
  _logger.println();

  // We wrap the callbacks so we can intercept messages intended for the WT32
  _onConfig = config;
  _onCommand = command;

  // Set up network and obtain an IP address
  byte mac[6];
  _initialiseNetwork(mac);

  // Set up MQTT (don't attempt to connect yet)
  _initialiseMqtt(mac);

  // Set up the REST API
  _initialiseRestApi();
}

void OXRS_WT32::loop(void)
{
  // Check our network connection
  if (_isNetworkConnected())
  {
    // Maintain our DHCP lease
#if defined(ETH_MODE)
    Ethernet.maintain();
#endif

    // Handle any MQTT messages
    _mqtt.loop();

    // Handle any REST API requests
#if defined(ETH_MODE)
    EthernetClient client = _server.available();
    _api.loop(&client);
#else
    WiFiClient client = _server.available();
    _api.loop(&client);
#endif
  }

  // Publish any sensor telemetry
#if defined(I2C_SENSORS)
  _sensors.tele();
#endif
}

void OXRS_WT32::setConfigSchema(JsonVariant json)
{
  _fwConfigSchema.clear();
  _mergeJson(_fwConfigSchema.as<JsonVariant>(), json);
}

void OXRS_WT32::setCommandSchema(JsonVariant json)
{
  _fwCommandSchema.clear();
  _mergeJson(_fwCommandSchema.as<JsonVariant>(), json);
}

boolean OXRS_WT32::publishStatus(JsonVariant json)
{
  // Exit early if no network connection
  if (!_isNetworkConnected()) { return false; }

  boolean success = _mqtt.publishStatus(json);
  return success;
}

boolean OXRS_WT32::publishTelemetry(JsonVariant json)
{
  // Exit early if no network connection
  if (!_isNetworkConnected()) { return false; }

  boolean success = _mqtt.publishTelemetry(json);
  return success;
}

size_t OXRS_WT32::write(uint8_t character)
{
  // Pass to logger - allows firmware to use `wt32.println("Log this!")`
  return _logger.write(character);
}

void OXRS_WT32::_initialiseNetwork(byte * mac)
{
  // Get WiFi base MAC address
  WiFi.macAddress(mac);

#if defined(ETH_MODE)
  // Ethernet MAC address is base MAC + 3
  // See https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/system.html#mac-address
  mac[5] += 3;
#endif

  // Update OLED display
#if defined(I2C_SENSORS)
  _sensors.oled(mac);
#endif

  // Format the MAC address for logging
  char mac_display[18];
  sprintf_P(mac_display, PSTR("%02X:%02X:%02X:%02X:%02X:%02X"), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

#if defined(ETH_MODE)
  _logger.print(F("[wt32] ethernet mac address: "));
  _logger.println(mac_display);

  // Initialise ethernet library
  Ethernet.init(ETHERNET_CS_PIN);

  // Reset Wiznet W5500
  pinMode(WIZNET_RST_PIN, OUTPUT);
  digitalWrite(WIZNET_RST_PIN, HIGH);
  delay(250);
  digitalWrite(WIZNET_RST_PIN, LOW);
  delay(50);
  digitalWrite(WIZNET_RST_PIN, HIGH);
  delay(350);

  // Connect ethernet and get an IP address via DHCP
  if (!Ethernet.begin(mac, DHCP_TIMEOUT_MS, DHCP_RESPONSE_TIMEOUT_MS))
  {
    if (Ethernet.hardwareStatus() == EthernetNoHardware) 
    {
      _logger.println(F("[wt32] ethernet shield not found"));
    } 
    else if (Ethernet.linkStatus() == LinkOFF) 
    {
      _logger.println(F("[wt32] ethernet cable not connected"));
    } 
    else 
    {
      _logger.println(F("[wt32] failed to setup ethernet using DHCP"));
    }
    return;
  }
  
  IPAddress ipAddress = Ethernet.localIP();
#else
  _logger.print(F("[wt32] wifi mac address: "));
  _logger.println(mac_display);

  // Ensure we are in the correct WiFi mode
  WiFi.mode(WIFI_STA);

  // Connect using saved creds, or start captive portal if none found
  // NOTE: Blocks until connected or the portal is closed
  WiFiManager wm;
  if (!wm.autoConnect("OXRS_WiFi", "superhouse"))
  {
    _logger.println(F("[wt32] failed to connect to wifi acces point"));
    return;
  }
  
  IPAddress ipAddress = WiFi.localIP();
#endif

  // Update OLED display
#if defined(I2C_SENSORS)
  _sensors.oled(ipAddress);
#endif

  _logger.print(F("[wt32] ip address: "));
  _logger.println(ipAddress);
}

void OXRS_WT32::_initialiseMqtt(byte *mac)
{
  // NOTE: this must be called *before* initialising the REST API since
  //       that will load MQTT config from file, which has precendence

  // Set the default client ID to last 3 bytes of the MAC address
  char clientId[32];
  sprintf_P(clientId, PSTR("%02x%02x%02x"), mac[3], mac[4], mac[5]);
  _mqtt.setClientId(clientId);

  // Register our callbacks
  _mqtt.onConnected(_mqttConnected);
  _mqtt.onDisconnected(_mqttDisconnected);
  _mqtt.onConfig(_mqttConfig);
  _mqtt.onCommand(_mqttCommand);

  // Start listening for MQTT messages
  _mqttClient.setCallback(_mqttCallback);
}

void OXRS_WT32::_initialiseRestApi(void)
{
  // NOTE: this must be called *after* initialising MQTT since that sets
  //       the default client id, which has lower precendence than MQTT
  //       settings stored in file and loaded by the API

  // Set up the REST API
  _api.begin();
  
  // Register our callbacks
  _api.onAdopt(_apiAdopt);
  
#if defined(LV_USE_SNAPSHOT)
  // Custom endpoint for downloading a snapshot of the WT32 display
  _api.get("/snapshot.bmp", &_getApiSnapshot);
#endif

  // Start listening
  _server.begin();
}

boolean OXRS_WT32::_isNetworkConnected(void)
{
#if defined(ETH_MODE)
  return Ethernet.linkStatus() == LinkON;
#else
  return WiFi.status() == WL_CONNECTED;
#endif
}

connectionState_t OXRS_WT32::getConnectionState(void)
{
  if (_isNetworkConnected())
  {
    return _mqtt.connected() ? CONNECTED_MQTT : CONNECTED_IP;
  }

  return CONNECTED_NONE;
}

void OXRS_WT32::getIPAddressTxt(char *buffer)
{
  IPAddress ip = IPAddress(0, 0, 0, 0);

  if (_isNetworkConnected())
  {
#if defined(ETH_MODE)
    ip = Ethernet.localIP();
#else
    ip = WiFi.localIP();
#endif
  }

  if (ip[0] == 0)
  {
    sprintf(buffer, "---.---.---.---");
  }
  else
  {
    sprintf(buffer, "%03d.%03d.%03d.%03d", ip[0], ip[1], ip[2], ip[3]);
  }
}

void OXRS_WT32::getMACAddressTxt(char *buffer)
{
  byte mac[6];

#if defined(ETH_MODE)
  Ethernet.MACAddress(mac);
#else
  WiFi.macAddress(mac);
#endif

  sprintf(buffer, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void OXRS_WT32::getMQTTTopicTxt(char *buffer)
{
  char topic[64];
 
  if (!_mqtt.connected())
  {
    sprintf(buffer, "-/------");
  }
  else
  {
    _mqtt.getWildcardTopic(topic);
    strcpy(buffer, "");
    strncat(buffer, topic, 39);
  }
}