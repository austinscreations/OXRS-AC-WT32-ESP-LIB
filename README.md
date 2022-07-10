# OXRS-AC-WT32-ESP-LIB
Library for interfacing WT32-SC01 touch screen into OXRS

## Currently supports:
Project: https://github.com/austinscreations/WT32-SC01_POE <br>
Firmware: https://github.com/austinscreations/OXRS-AC-TP32-ESP-FW <br>
A library: https://github.com/austinscreations/OXRS-AC-I2CSensors-ESP-LIB

## Dependencies:
``` c++
#include <OXRS_MQTT.h>                // For MQTT     - https://github.com/OXRS-IO/OXRS-IO-MQTT-ESP32-LIB
#include <OXRS_API.h>                 // For REST API - https://github.com/OXRS-IO/OXRS-IO-API-ESP32-LIB
#include <ArduinoJson.h>
#include <MqttLogger.h>               // For logging  - androbi/MqttLogger

#if defined(ETHMODE)
#include <Ethernet.h>                 // for ethernet - https://github.com/OXRS-IO/Ethernet
#endif

#if defined(WIFIMODE)
#include <WiFiManager.h>              // For WiFi AP config - https://github.com/tzapu/wifiManager
#include <WiFi.h>                     // for wifi
#endif

#if defined(USESENSORS)
#include <Adafruit_MCP9808.h>         // For temp sensor
#include <Adafruit_SSD1306.h>         // For OLED display
#include <Adafruit_SHT4x.h>           // For SHT40 Temp / humidity sensor
#include <RTClib.h>                   // For PCF8523 RTC
#include <BH1750.h>                   // For BH1750 lux sensor - non-adafruit library: https://github.com/claws/BH1750
#include <OXRS_SENSORS.h>             // For OXRS i2c sensors https://github.com/austinscreations/OXRS-AC-I2CSensors-ESP-LIB
#include <Wire.h>                     // For I2C 
#endif
```

## The core of OXRS_WT32:
```c++
#include <OXRS_WT32.h>                // WT32 support

// initialize the library instance
OXRS_WT32 wt32;

// To start your code you'll likely start with serial debugging - this line can initilaize serial and print out the firmware env information in your platformIO config
wt32.initialiseSerial();

// This starts the core of the library conencting to network (wifi or ethernet) and then starting the mqtt and api libaries
wt32.begin();

// adds config schema to the schema payload
// placed after JsonObject properties = configSchema.createNestedObject("properties");
// your device specific schema can be placed after
wt32.setConfigSchema(properties);

// adds command schema to the schema payload
// placed after JsonObject properties = commandSchema.createNestedObject("properties");
// your device specific schema can be placed after
wt32.setCommandSchema(properties);

// returns the connection state - CONNECTED_NONE, CONNECTED_IP, CONNECTED_MQTT 
wt32.getConnectionState();
```

## For your platformIO INI file you'll have options for using the sensors, setting wifi or ethernet mode, or the serial baud rate

```ini
build_flags =
  ; -- firmware info
	-DFW_NAME="${firmware.name}"
	-DFW_SHORT_NAME="${firmware.short_name}"
	-DFW_MAKER="${firmware.maker}"
	; -DFW_GITHUB_URL="${firmware.github_url}" ; not required for oxrs but can be added if availble

  ; -- wt32 variables
	-DSERIAL_BAUD_RATE=115200
  -DUSESENSORS=1 ; activates the use of OXRS sensor library - ensure to declare the required libraries / dependacies

  -DWIFIMODE=1 ; used when device will operate in wifi mode
  -DETHMODE=1 ; used when device will operate in ethernet mode
```
