# ESP32-C3 Elegoo Centauri Carbon 2 Monitor

An Arduino-based external 320×240 TFT monitor for the Elegoo Centauri Carbon 2, powered by an ESP32-C3. Printer data comes from Home Assistant: initial values are loaded over REST, then live updates arrive through a printer-only `subscribe_trigger` WebSocket subscription.

The monitor presents a dedicated printing view, pause and completion views, and an idle dashboard with current weather and a two-day forecast. A local Web UI handles Home Assistant settings, status information, maintenance, and diagnostics. WiFiManager provides WiFi-only captive-portal provisioning when stored credentials are missing or cannot connect during startup.

> Home Assistant and the required Centauri Carbon 2 entities are prerequisites. The monitor does not communicate with the printer directly.

## 📷 Screenshots and photos

Images can be added under `docs/images/` using these filenames:

| Printing screen | Idle and weather screen |
|---|---|
| ![Printing screen placeholder](docs/images/printing-screen.jpg) | ![Idle and weather screen placeholder](docs/images/idle-weather-screen.jpg) |

| Completion screen | Web UI |
|---|---|
| ![Completion screen placeholder](docs/images/completion-screen.jpg) | ![Web UI placeholder](docs/images/web-ui.jpg) |

## ✨ Features

- Live printer data from Home Assistant over a targeted WebSocket `subscribe_trigger`
- Initial printer-state loading through the Home Assistant REST API
- Printing screen with progress, remaining time/calculated ETA, layers, nozzle/bed/chamber temperatures, and print-speed mode/value
- Explicit printer state machine for idle, printing, paused, and completed prints
- Preheating treated as active and pausing/paused shown on a dedicated pause screen
- Successful completion screen with completion time and last valid layer information, displayed for 60 seconds
- Cancellation through explicit `stopped` status rather than progress-based inference
- Idle dashboard with long date, HA status, boot/uptime diagnostics, current weather, and forecasts for the next two days
- Weather icons drawn with Adafruit GFX primitives—no bitmap assets or Unicode weather glyphs
- Persistent, versioned JSON application configuration in LittleFS
- WiFiManager captive portal for initial WiFi setup and deliberate credential recovery
- Lightweight local Web UI with Status, Configuration, and Maintenance sections
- Automatic WiFi recovery and HA reconnect, authentication, and resubscription
- Recovery from repeated early WebSocket disconnects by reconstructing the client after three consecutive failures
- Serial diagnostics, boot counter, reset reason, heap statistics, and bounded temporary StateTrace logging
- Firmware version, base Git revision, dirty-build marker, and compiler build timestamp
- Dirty-region display updates to avoid periodic full-screen flicker

## 🔌 Hardware and wiring

The sketch targets an ESP32-C3 connected to an ILI9341 320×240 TFT in landscape orientation. The source identifies the original controller as an ESP32-C3 SuperMini, while Arduino IDE builds use the generic **ESP32C3 Dev Module** profile.

| ILI9341 signal | ESP32-C3 pin | Source constant | Notes |
|---|---:|---|---|
| SCK / CLK | GPIO 4 | `TFT_SCK` | SPI clock |
| MOSI / SDI | GPIO 6 | `TFT_MOSI` | ESP32-C3 to display data |
| CS | GPIO 7 | `TFT_CS` | Display chip select |
| DC / RS | GPIO 3 | `TFT_DC` | Data/command select |
| RST | GPIO 1 | `TFT_RST` | Display reset |
| MISO / SDO | GPIO 5 | `TFT_MISO` | Shared SPI MISO for the touch controller; the TFT does not use it |

The firmware uses the ESP32-C3 `FSPI` peripheral and TFT rotation `3`. Power, ground, and backlight wiring are not defined in the source; follow the electrical requirements of your specific modules.

### Resistive touch controller — Phase 1

Phase 1 provides non-blocking raw touch reads, calibration diagnostics, and simple tap detection. It does not yet provide gestures, printer controls, maintenance actions, or a dedicated on-device diagnostics page.

| Touch signal | ESP32-C3 pin | Notes |
|---|---:|---|
| T_CLK | GPIO 4 | Shared with TFT SCK |
| T_DIN | GPIO 6 | Shared with TFT MOSI |
| T_DO | GPIO 5 | Shared SPI MISO |
| T_CS | GPIO 10 | Dedicated touch chip select |
| T_IRQ | Not connected | Phase 1 uses non-blocking polling |

The TFT and XPT2046 use separate active-low chip-select lines. Touch transactions run at 2 MHz through the installed `XPT2046_Touchscreen` library.

Touch calibration is intentionally disabled until measured values are available. With `TOUCH_VERBOSE_LOGGING` enabled in `TouchInput.cpp`, press the active screen edges and corners and record the reported raw X/Y values. Enter the measured limits in `TOUCH_RAW_X_MIN`, `TOUCH_RAW_X_MAX`, `TOUCH_RAW_Y_MIN`, and `TOUCH_RAW_Y_MAX`; adjust `TOUCH_SWAP_AXES`, `TOUCH_INVERT_X`, and `TOUCH_INVERT_Y` as required for rotation 3, then set `TOUCH_CALIBRATION_VALID` to `true`. Disable verbose logging after calibration.

## Software requirements

| Dependency | Purpose | Repository evidence for version |
|---|---|---|
| ESP32 Arduino core | ESP32-C3, WiFi, HTTP, SPI, Preferences, LittleFS, mDNS, FreeRTOS, and diagnostics | Not pinned |
| Adafruit GFX Library | Text and primitive graphics | Not pinned |
| Adafruit ILI9341 | TFT driver | Not pinned |
| ArduinoJson | REST, WebSocket, weather, and configuration JSON | Not pinned |
| ArduinoWebsockets | Home Assistant WebSocket client | Not pinned |
| WiFiManager | WiFi provisioning and credential reset | 2.0.17, or a compatible ESP32-capable release |
| XPT2046_Touchscreen | Phase 1 resistive touch polling and raw samples | 1.4, or a compatible release |

The GFX fonts used by the sketch are supplied by Adafruit GFX. PlatformIO is not used.

## 🛠️ Arduino IDE configuration

Open `ESP32_C3_ILI9341_Elegoo_Monitor.ino` directly in Arduino IDE and select:

| Setting | Required value |
|---|---|
| Board | **ESP32C3 Dev Module** |
| Flash Size | **4 MB (32 Mb)** |
| Partition Scheme | **Huge APP (3MB No OTA/1MB SPIFFS)** |

The firmware is larger than the default ESP32-C3 application partition, so Huge APP is required. It provides approximately 3 MB for the application and retains an approximately 1 MB filesystem partition used through LittleFS for configuration and StateTrace diagnostics.

Huge APP has no OTA slot. Upload firmware through the normal Arduino IDE USB/serial workflow. Although the menu labels the filesystem partition “SPIFFS,” the application mounts it with `LittleFS`.

## Home Assistant requirements

Printer entities are provided by [danielcherubini/elegoo-homeassistant](https://github.com/danielcherubini/elegoo-homeassistant). This monitor consumes the Home Assistant entities created by that integration and does not communicate directly with the printer.

The configurable printer entity prefix adapts the monitor to the object ID assigned by Home Assistant. Its default is `centauri_carbon_2`, producing IDs such as `sensor.centauri_carbon_2_druckstatus`. Enter only the object-ID prefix, without the `sensor.` domain or suffix. Valid prefixes contain lowercase letters, digits, and underscores.

The following suffixes are fixed and combined with `sensor.<configured-prefix>`:

| Purpose | Fixed suffix |
|---|---|
| Remaining print time | `_remaining_print_time` |
| Total layers | `_total_layers` |
| Current layer | `_current_layer` |
| Percent complete | `_percent_complete` |
| Generic current status | `_aktueller_status` |
| Authoritative print status | `_druckstatus` |
| Print error | `_druckfehler` |
| Error reason | `_fehlerstatusgrund` |
| Chamber/box temperature | `_box_temp` |
| Nozzle temperature | `_nozzle_temperature` |
| Bed temperature | `_bed_temperature` |
| Print speed | `_print_speed` |

An empty or invalid printer prefix safely disables printer REST/WebSocket communication while leaving WiFi, weather, and the Web UI available.

Weather can use any compatible Home Assistant `weather.*` entity that supports the forecast service response required by `weather.get_forecasts`. Development and testing used [FL550/dwd_weather](https://github.com/FL550/dwd_weather), but that integration is optional. The weather entity is configured through the Web UI and defaults to empty for new installations; empty disables weather retrieval without affecting printer monitoring.

### Long-Lived Access Token

A Home Assistant Long-Lived Access Token (LLAT) is required for authenticated REST and WebSocket access. Create one in Home Assistant and enter it on the monitor's Web UI Configuration page.

The Web UI never displays the stored token back in plaintext. The password field is intentionally empty: submitting it empty preserves the current token, while a non-empty value replaces it.

## First-time setup

1. Install the board package and required libraries.
2. Select the required Arduino IDE settings above.
3. Run `powershell -ExecutionPolicy Bypass -File tools/build_prep.ps1` from the project root.
4. Compile and upload `ESP32_C3_ILI9341_Elegoo_Monitor.ino`.
5. The ESP32-C3 first attempts to use WiFi credentials already stored by its WiFi subsystem.
6. If startup connection fails, join **Elegoo-Monitor-Setup** and use its WiFi-only captive portal.
7. Open the Web UI using the IP printed over Serial or the default `http://cc2-monitor.local/`. If the name was previously changed, use the configured mDNS name.
8. In **Configuration**, enter the HA host/IP, port, LLAT, POSIX timezone, printer entity prefix, and optional weather entity.
9. Save, restart as instructed, then use **Status** to verify printer data, authentication, subscription, and weather.

The Web UI remains independent of Home Assistant availability once WiFi is connected.

## Web UI

The lightweight HTTP server listens on port 80 and publishes an mDNS HTTP service using the configured device name.

### Status

- firmware identifier, clean/dirty state, and compiler build date/time
- WiFi state and ESP IP
- HA WebSocket, authentication, and subscription state
- printer entity prefix and whether its configuration is valid
- resolved printer state and both raw status values
- weather status
- StateTrace logging state
- boot count, uptime, reset reason, free heap, and minimum free heap

### Configuration

- device/mDNS name
- HA host/IP, port, and LLAT
- POSIX timezone
- printer entity prefix
- weather entity and refresh interval
- ETA/remaining-time switch interval
- StateTrace logging enabled/disabled

Configuration is stored as versioned JSON in `/config.json` on LittleFS. Saved changes take effect after restart. Weather refresh values outside 1 minute–24 hours and ETA switch values outside 1–60 seconds fall back to compiled defaults.

### Maintenance

- restart the device
- clear application configuration and restart with compiled defaults
- reset stored WiFi credentials after a confirmation page without clearing application configuration
- show, download, and confirm clearing of the temporary StateTrace log

Clearing application configuration and resetting WiFi credentials are separate operations.

## Printer state handling

The authoritative source is `sensor.<configured-prefix>_druckstatus`:

| Normalized HA value | Monitor behavior |
|---|---|
| `idle` | Idle before a print; intentionally ignored while an active print is established |
| `preheating` | Active printing view |
| `printing` | Printing view |
| `pausing`, `paused` | Paused view |
| `stopping` | Preserve active state while waiting for an explicit terminal value |
| `stopped` | Explicit cancellation; return directly to idle |
| `complete` | Explicit success; show PRINT COMPLETE for 60 seconds when eligibility checks pass |
| `unavailable` | Preserve the current resolved state |

Real purge and color-change sequences can report transient `idle`, so it is ignored during an established active job. Completion is never inferred from progress reaching 100%.

HA may replace `complete` or `stopped` with `idle` before the main loop runs. Terminal events are therefore latched on arrival and consumed in event order. A later `stopped` overrides an earlier pending completion. The completion view snapshots the last valid current and total layers; invalid later values do not erase the snapshot, and missing totals are not inferred.

## Reliability and recovery

- Initial REST requests use finite timeouts; malformed responses or unavailable HA services do not reboot the ESP32-C3.
- Live updates use a printer-only WebSocket subscription rather than a broad event stream.
- Disconnects trigger reconnect, reauthentication, and resubscription with backoff capped at 30 seconds.
- Repeated connections that close before authentication and subscription receive exponential backoff. After three consecutive early disconnects, the stale WebSocket client is reconstructed before retrying.
- Temporary `idle` and `unavailable` values preserve an established active print state.
- Runtime WiFi loss uses `WiFi.reconnect()` on a five-second retry interval rather than immediately starting an AP.
- Weather uses separate short-timeout HTTP work and the Web UI remains independent of HA availability.

## Diagnostics and StateTrace

Serial diagnostics use 115200 baud and include boot/reset information, printer state, WiFi/WebSocket/authentication/subscription state, connection and event counters, uptime, free heap, and minimum free heap. The idle display shows boot count, uptime, reset reason, and a compact firmware ID.

Temporary `StateTrace` logging uses `/state_trace.log` in LittleFS. It records boot/reset, both raw status changes, resolved transitions, printer error/reason changes, WebSocket lifecycle and recovery, ignored active idle, and terminal-signal latching/consumption. It does not intentionally record credentials.

StateTrace logging is enabled by default for the current development firmware and can be enabled or disabled on the Web UI Configuration page. Disabling it stops new writes without deleting the existing file; size, download, and clear actions remain available. Re-enabling resumes normal bounded logging. Serial diagnostics are unaffected.

Lines use synchronized local time when available, otherwise `UPTIME_MS=<value>`. The file is bounded to approximately 64 KiB; when the next write would exceed the limit, the old file is removed and logging restarts. Logging failure is non-fatal.

Use **Maintenance → Temporary state trace diagnostics** to see its size, download `state_trace.log`, or clear it after confirmation. The server exposes only this fixed trace path, not arbitrary filesystem files.

## Firmware versioning

The semantic firmware version remains manually maintained in `Version.h`:

```cpp
#define FIRMWARE_VERSION "0.1.0-dev"
```

Git metadata is generated from the repository into the ignored `GeneratedVersion.h` file. Before
every Arduino IDE compilation, run:

```powershell
powershell -ExecutionPolicy Bypass -File tools/build_prep.ps1
```

The script uses `git rev-parse --short HEAD`, `git status --porcelain`, and the current local branch.
It writes only the short revision, dirty flag, and branch—never repository paths, remote URLs,
usernames, or credentials. A dirty tree appends `+` to the displayed revision; a clean tree omits it.
Compiler date/time continues to use `__DATE__` and `__TIME__`.

Arduino IDE has no reliable project-local pre-build hook, so the prep command is an explicit build
step. Codex builds must run `tools/build_prep.ps1` automatically immediately before invoking Arduino
CLI. If `GeneratedVersion.h` is absent, compilation still succeeds and displays `unknown`; it never
falls back to a stale hard-coded revision.

Before a tested baseline:

1. Generate metadata and test the dirty build on hardware.
2. Commit the tested source changes.
3. Run `tools/build_prep.ps1` again so the new clean commit and state are embedded.
4. Compile and flash again, verify both displayed identifiers, then tag.

Suggested versions: `MAJOR.MINOR.PATCH` for stable, `MAJOR.MINOR.PATCH-dev` for development, and `MAJOR.MINOR.PATCH-test.N` for test builds.

## Project structure

| Module | Responsibility |
|---|---|
| `ESP32_C3_ILI9341_Elegoo_Monitor.ino` | Setup, initialization, and main loop |
| `AppState.h/.cpp` | Shared objects, cached entity IDs, pins, data, counters, timers, and dirty flags |
| `Config.h/.cpp` | Configuration structure, version, validation, and defaults |
| `ConfigStore.h/.cpp` | LittleFS JSON load/save/clear |
| `Diagnostics.h/.cpp` | Boot counter, reset reason, uptime, and Serial diagnostics |
| `Display.h/.cpp` | General TFT layouts and dirty-region rendering |
| `HomeAssistant.h/.cpp` | Printer REST load, WiFi provisioning/recovery, WebSocket authentication/subscription/recovery |
| `PrinterData.h/.cpp` | Entity parsing, state machine, terminal latching, layer snapshots, and formatting |
| `StateTrace.h/.cpp` | Bounded temporary LittleFS trace |
| `TimeHelpers.h/.cpp` | NTP, timezone, clock, and date |
| `Weather.h/.cpp` | Weather REST task, forecast cache, primitive icons, and weather-specific drawing |
| `WebUI.h/.cpp` | HTTP status/configuration/maintenance UI, mDNS, and trace download |
| `Version.h` | Manual semantic version, generated-metadata fallback, and identifier macros |
| `tools/build_prep.ps1` | Generates ignored Git revision, dirty state, and branch metadata before builds |

## Known limitations

- Home Assistant and compatible entities from the Elegoo integration are required; there is no direct printer connection.
- Huge APP has no OTA slot, so firmware upload is USB/serial only.
- Arduino IDE requires an explicit `tools/build_prep.ps1` step before compiling Git metadata.
- Printer entity suffixes are fixed; the shared printer prefix and weather entity are configurable.
- HA traffic uses unencrypted `http://` and `ws://`; deploy only on a trusted network unless transport security is added.
- Touch input is not implemented.
- StateTrace is temporary and restarts instead of rotating when its size limit is reached.

## Possible future work

These are ideas, not implemented features:

- touch-controller support
- an on-device diagnostics/status page
- individually configurable printer entity suffixes if future integrations require them
- optional build tooling for version metadata while retaining Arduino IDE compatibility
- secure HA transport where supported

## 🔐 Security

- Never commit WiFi credentials or a Home Assistant LLAT to Git.
- Treat local configuration exports, backups, downloaded device data, and configuration screenshots as sensitive.
- The LLAT field loads blank; leaving it blank preserves the stored value.
- Rotate a token immediately if exposed in source, logs, screenshots, or repository history.
- Perform a repository-history secret audit before any public release; deleting a secret from the current source does not remove it from existing commits.
- Keep the unauthenticated local Web UI on a trusted network.

## License

This project is licensed under the [MIT License](LICENSE). The Home Assistant integrations and Arduino libraries it consumes remain subject to their own licenses.
