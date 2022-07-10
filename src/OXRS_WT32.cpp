/*
 * OXRS_WT32.cpp
 */

#include <OXRS_WT32.h>

/*--------------------------- Instantiate Global Objects -----------------*/
#if defined(ETHMODE)
EthernetClient _client;
EthernetServer _server(REST_API_PORT);
#endif

#if defined(WIFIMODE)
WiFiClient _client;
WiFiServer _server(REST_API_PORT);
#endif

// MQTT
PubSubClient _mqttClient(_client);
OXRS_MQTT _mqtt(_mqttClient);

// REST API
OXRS_API _api(_mqtt);

// Logging
MqttLogger _logger(_mqttClient, "log", MqttLoggerMode::MqttAndSerial);

// I2C sensors
#if defined(USESENSORS)
OXRS_SENSORS _sensors(_mqtt);
#endif

// Supported firmware config and command schemas
DynamicJsonDocument _fwConfigSchema(2048);
DynamicJsonDocument _fwCommandSchema(2048);

// MQTT callbacks wrapped by _mqttConfig/_mqttCommand
jsonCallback _onConfig;
jsonCallback _onCommand;


/*--------------------------- JSON builders -----------------*/
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

  system["flashChipSizeBytes"] = ESP.getFlashChipSize();
  system["heapFreeBytes"] = ESP.getFreeHeap();

  system["heapUsedBytes"] = ESP.getHeapSize();
  system["heapMaxAllocBytes"] = ESP.getMaxAllocHeap();

  system["sketchSpaceUsedBytes"] = ESP.getSketchSize();
  system["sketchSpaceTotalBytes"] = ESP.getFreeSketchSpace();

  system["fileSystemUsedBytes"] = SPIFFS.usedBytes();
  system["fileSystemTotalBytes"] = SPIFFS.totalBytes();

}

void _getNetworkJson(JsonVariant json)
{
  JsonObject network = json.createNestedObject("network");
  
  byte mac[6];
  
  #if defined(ETHMODE)
  network["mode"] = "ethernet";
  Ethernet.MACAddress(mac);
  network["ip"] = Ethernet.localIP();
  #elif defined(WIFIMODE)
  network["mode"] = "wifi";
  WiFi.macAddress(mac);
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
  #if defined(USESENSORS)
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

  JsonObject restart = properties.createNestedObject("restart");
  restart["type"] = "boolean";
  restart["description"] = "Restart the controller";

  // Add any sensor commands
  #if defined(USESENSORS)
  _sensors.setCommandSchema(properties);
  #endif
}

void _apiAdopt(JsonVariant json)
{
  // Build device adoption info
  _getFirmwareJson(json);
  _getSystemJson(json);
  _getNetworkJson(json);
  _getConfigSchemaJson(json);
  _getCommandSchemaJson(json);
}

/*--------------------------- Initialisation -------------------------------*/
void _initialiseRestApi(void)
{
  // NOTE: this must be called *after* initialising MQTT since that sets
  //       the default client id, which has lower precendence than MQTT
  //       settings stored in file and loaded by the API

  // Set up the REST API
  _api.begin();

  // Register our callbacks
  _api.onAdopt(_apiAdopt);

  _server.begin();
}

/*--------------------------- MQTT/API -----------------*/
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
  _logger.println("[wt32.cpp] mqtt connected");
}

void _mqttDisconnected(int state) 
{
  // Log the disconnect reason
  // See https://github.com/knolleary/pubsubclient/blob/2d228f2f862a95846c65a8518c79f48dfc8f188c/src/PubSubClient.h#L44
  switch (state)
  {
    case MQTT_CONNECTION_TIMEOUT:
      _logger.println(F("[wt32.cpp] mqtt connection timeout"));
      break;
    case MQTT_CONNECTION_LOST:
      _logger.println(F("[wt32.cpp] mqtt connection lost"));
      break;
    case MQTT_CONNECT_FAILED:
      _logger.println(F("[wt32.cpp] mqtt connect failed"));
      break;
    case MQTT_DISCONNECTED:
      _logger.println(F("[wt32.cpp] mqtt disconnected"));
      break;
    case MQTT_CONNECT_BAD_PROTOCOL:
      _logger.println(F("[wt32.cpp] mqtt bad protocol"));
      break;
    case MQTT_CONNECT_BAD_CLIENT_ID:
      _logger.println(F("[wt32.cpp] mqtt bad client id"));
      break;
    case MQTT_CONNECT_UNAVAILABLE:
      _logger.println(F("[wt32.cpp] mqtt unavailable"));
      break;
    case MQTT_CONNECT_BAD_CREDENTIALS:
      _logger.println(F("[wt32.cpp] mqtt bad credentials"));
      break;      
    case MQTT_CONNECT_UNAUTHORIZED:
      _logger.println(F("[wt32.cpp] mqtt unauthorised"));
      break;      
  }
}

void _jsonCommand(JsonVariant json)
{
  // Pass on to the firmware callback
  if (_onCommand)
  {
    _onCommand(json);
  }

  if (json.containsKey("restart") && json["restart"].as<bool>())
  {
    ESP.restart();
  }

  // Let the sensors handle any commands
  #if defined(USESENSORS)
  _sensors.cmnd(json);
  #endif
}

void _jsonConfig(JsonVariant json)
{
  // Pass on to the firmware callback
  if (_onConfig)
  {
    _onConfig(json);
  }

  // Let the sensors handle any config
  #if defined(USESENSORS)
  _sensors.conf(json);
  #endif
}

void _mqttCallback(char * topic, uint8_t * payload, unsigned int length) 
{
  // Pass this message down to our MQTT handler
  _mqtt.receive(topic, payload, length);
}

void _initialiseMqtt()
{
  byte mac[6];
  WiFi.macAddress(mac);

  // Set the default client id to the last 3 bytes of the MAC address
  char clientId[32];
  sprintf_P(clientId, PSTR("%02x%02x%02x"), mac[3], mac[4], mac[5]);  
  _mqtt.setClientId(clientId);
  
  // Register our callbacks
  _mqtt.onConnected(_mqttConnected);
  _mqtt.onDisconnected(_mqttDisconnected);
  _mqtt.onConfig(_jsonConfig);
  _mqtt.onCommand(_jsonCommand);  

  // Start listening for MQTT messages
  _mqttClient.setCallback(_mqttCallback);  
}

/*--------------------------- Network -------------------------------*/
#if defined(WIFIMODE)
void _initialiseWifi()
{
  // Ensure we are in the correct WiFi mode
  WiFi.mode(WIFI_STA);

  // Get WiFi base MAC address
  byte mac[6];
  WiFi.macAddress(mac);

  // Display the MAC address on serial
  char mac_display[18];
  sprintf_P(mac_display, PSTR("%02X:%02X:%02X:%02X:%02X:%02X"), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  _logger.print(F("[wt32.cpp] mac address: "));
  _logger.println(mac_display);

  // Update OLED display
  #if defined(USESENSORS)
  _sensors.oled(mac);
  #endif

  // Connect using saved creds, or start captive portal if none found
  // Blocks until connected or the portal is closed
  WiFiManager wm;
  if (!wm.autoConnect("OXRS_WiFi", "superhouse"))
  {
    // If we are unable to connect then restart
    ESP.restart();
  }

  // Display IP address on serial
  _logger.print(F("[wt32.cpp] ip address: "));
  _logger.println(WiFi.localIP());

  // Update OLED display
  #if defined(USESENSORS)
  _sensors.oled(WiFi.localIP());
  #endif
}
#endif

#if defined(ETHMODE)
void _initialiseEthernet()
{
  // Get ESP base MAC address
  byte mac[6];
  WiFi.macAddress(mac);
  
  // Ethernet MAC address is base MAC + 3
  // See https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/system.html#mac-address
  mac[5] += 3;

  // Display the MAC address on serial
  char mac_display[18];
  sprintf_P(mac_display, PSTR("%02X:%02X:%02X:%02X:%02X:%02X"), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  _logger.print(F("[wt32.cpp] mac address: "));
  _logger.println(mac_display);

  // Update OLED display
  #if defined(USESENSORS)
  _sensors.oled(mac);
  #endif

  _logger.println(F("[wt32.cpp] Starting Ethernet DHCP Connection"));

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

  // Get an IP address via DHCP
  _logger.print("[wt32.cpp] ip address: ");
  if (!Ethernet.begin(mac, DHCP_TIMEOUT_MS, DHCP_RESPONSE_TIMEOUT_MS))
  {
    if (Ethernet.hardwareStatus() == EthernetNoHardware) {
      _logger.println(F("ethernet shield not found"));
    } else if (Ethernet.linkStatus() == LinkOFF) {
      _logger.println(F("ethernet cable not connected"));
    } else {
      _logger.println(F("failed to setup ethernet using DHCP"));
    }
    return;
  }
  
  // Display IP address on serial
  _logger.println(Ethernet.localIP());

  // Update OLED display
  #if defined(USESENSORS)
  _sensors.oled(Ethernet.localIP());
  #endif
}
#endif

/*
 *
 *  Main program
 *
 */
OXRS_WT32::OXRS_WT32(){}

size_t OXRS_WT32::write(uint8_t character)
{
  // Pass to logger - allows firmware to use `rack32.println("Log this!")`
  return _logger.write(character);
}

void OXRS_WT32::initialiseSerial()
{
  Serial.begin(SERIAL_BAUD_RATE);
  delay(2000);
  
  Serial.println(F("[WT32.cpp] starting up..."));

  DynamicJsonDocument json(128);
  _getFirmwareJson(json.as<JsonVariant>());

  Serial.print(F("[WT32.cpp] "));
  serializeJson(json, Serial);
  Serial.println();
}

void OXRS_WT32::begin(jsonCallback config, jsonCallback command)
{
  // We wrap the callbacks so we can intercept messages intended for the Rack32
  _onConfig = config;
  _onCommand = command;

  #if defined(ETHMODE)
  _initialiseEthernet();
  #elif defined(WIFIMODE)
  _initialiseWifi();
  #endif

  // Set up MQTT (don't attempt to connect yet)
  _initialiseMqtt();

  // Set up the REST API once we have an IP address
  _initialiseRestApi();
}

void OXRS_WT32::loop() 
{
  if (_isNetworkConnected())
  {
    _mqtt.loop();
    // Handle any API requests
    #if defined(WIFIMODE)
    WiFiClient _client = _server.available();
    _api.loop(&_client);
    #elif defined(ETHMODE)
    Ethernet.maintain();
    EthernetClient _client = _server.available();
    _api.loop(&_client);
    #endif
  }

}

/*--------------------------- Network -------------------------------*/
boolean OXRS_WT32::_isNetworkConnected()
{
#if defined(ETHMODE)
  return Ethernet.linkStatus() == LinkON;
#elif defined(WIFIMODE)
  return WiFi.status() == WL_CONNECTED;
#endif
}

connectionState_t OXRS_WT32::getConnectionState(void)
{
  if(_isNetworkConnected())
  {
    if (_mqtt.connected())
     return CONNECTED_MQTT;
    else
      return CONNECTED_IP;
  }
  return CONNECTED_NONE;
}

/*--------------------------- API -------------------------------*/
void OXRS_WT32::restartApi(void)
{
  _api.begin();
}

/*--------------------------- MQTT -------------------------------*/

void OXRS_WT32::setConfigSchema(JsonVariant json)
{
  _mergeJson(_fwConfigSchema.as<JsonVariant>(), json);
}

void OXRS_WT32::setCommandSchema(JsonVariant json)
{
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

/*--------------------------- LVGL -------------------------------*/
void OXRS_WT32::getIPAddressTxt(char *buffer)
{
  IPAddress ip = IPAddress(0, 0, 0, 0);

  if (_isNetworkConnected())
  {
#if defined(ETHMODE)
    ip = Ethernet.localIP();
#elif defined(WIFIMODE)
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

#if defined(ETHMODE)
  Ethernet.MACAddress(mac);
#elif defined(WIFIMODE)
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