# ESP32-C3 Elegoo Monitor

## Required Arduino IDE build settings

Use these settings when compiling or uploading the sketch:

- **Board:** ESP32C3 Dev Module (ESP32-C3)
- **Flash Size:** 4 MB (32 Mb)
- **Partition Scheme:** Huge APP (3MB No OTA/1MB SPIFFS)

The Huge APP partition scheme is required because the current firmware does not fit in the default application partition. It retains a filesystem partition used by LittleFS for the persistent configuration file.

The selected partition scheme does not provide an OTA update slot. Upload firmware through the normal Arduino IDE serial/USB upload process.

Open `ESP32_C3_ILI9341_Elegoo_Monitor.ino` directly in the Arduino IDE. PlatformIO is not used by this project.

## Required library

- **WiFiManager:** version 2.0.17 or a compatible release supporting ESP32

## WiFi provisioning

The firmware first tries credentials stored by the ESP32 WiFi subsystem. If no credentials exist or the bounded connection attempt fails, it starts the `Elegoo-Monitor-Setup` configuration access point and captive portal.

The captive portal configures WiFi only. Home Assistant and application settings remain in the separate LittleFS configuration managed through the normal device Web UI.
