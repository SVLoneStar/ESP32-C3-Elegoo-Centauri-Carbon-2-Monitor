# AGENTS.md

## Project

Elegoo Centauri Carbon 2 external monitor based on an ESP32-C3 and an
ILI9341 320x240 TFT.

The firmware displays live printer information from Home Assistant,
weather information while the printer is idle, diagnostics, and provides
a local configuration Web UI.

This project currently has a known-good working baseline.

---

## Primary Goal

Reliability is more important than adding features quickly.

Preserve existing working behavior unless a requested change explicitly
requires modifying it.

Prefer small, isolated changes over broad refactoring.

Do not redesign working subsystems merely because another implementation
would appear cleaner.

---

## Build Environment

This is an Arduino IDE project.

Required settings:

- Board: ESP32C3 Dev Module
- Flash Size: 4 MB
- Partition Scheme: Huge APP (3MB No OTA/1MB SPIFFS)
- Filesystem: LittleFS
- Framework: Arduino
- IDE: Arduino IDE

Do NOT introduce PlatformIO.

Do NOT add PlatformIO configuration files.

Do NOT change the partition scheme unless explicitly requested.

The Huge APP partition is required because the firmware does not fit into
the default ESP32-C3 application partition.

OTA is currently not required and is unavailable with the selected
partition layout.

---

## Hardware

Current target:

- ESP32-C3
- ILI9341 TFT
- Resolution: 320x240
- Landscape orientation
- Display rotation and existing pin assignments must be preserved unless
  explicitly requested.

Existing display coordinates are considered calibrated for the physical
installation.

Do not change display rotation, TFT pins, SPI configuration, icon
positions, font choices, or calibrated coordinates as part of unrelated
changes.

---

## Project Structure

The firmware is intentionally split into modules.

Current major modules include:

- `ESP32_C3_ILI9341_Elegoo_Monitor.ino`
  - application setup and main loop

- `AppState.h/.cpp`
  - shared application state, objects, counters, timers, dirty flags and
    caches

- `Diagnostics.h/.cpp`
  - boot counter
  - reset reason
  - uptime
  - serial diagnostics

- `PrinterData.h/.cpp`
  - printer state
  - state parsing
  - printing detection
  - ETA / remaining-time formatting
  - speed formatting

- `TimeHelpers.h/.cpp`
  - NTP
  - timezone handling
  - clock/date helpers

- `Display.h/.cpp`
  - general TFT display rendering
  - printing screen
  - idle screen
  - general display icons
  - dirty-region rendering

- `HomeAssistant.h/.cpp`
  - WiFi recovery
  - Home Assistant REST access
  - WebSocket connection
  - authentication
  - `subscribe_trigger`
  - reconnect handling

- `Weather.h/.cpp`
  - Home Assistant weather data
  - forecast handling
  - weather refresh logic
  - weather-specific TFT drawing and weather icons currently implemented
    in `Weather.cpp`

- `Config.h/.cpp`
  - runtime configuration
  - defaults
  - configuration version

- `ConfigStore.h/.cpp`
  - LittleFS configuration persistence

- `WebUI.h/.cpp`
  - local HTTP configuration UI
  - status page
  - maintenance actions
  - mDNS

Keep functionality in the appropriate module.

Do not move unrelated code between modules unless there is a concrete
reason to do so.

Avoid turning `AppState` into a general dumping ground for application
logic.

---

## Home Assistant

Home Assistant communication is a critical working subsystem.

The current implementation has been tested and should be treated as
stable.

### WebSocket

Printer updates use Home Assistant WebSocket communication with:

- authentication using a Long-Lived Access Token
- `subscribe_trigger`
- printer-specific entity subscriptions
- reconnect handling
- re-authentication
- re-subscription
- reconnect backoff

Do NOT replace `subscribe_trigger` with `state_changed` subscriptions
unless explicitly requested.

Do NOT redesign the WebSocket implementation during unrelated work.

Do NOT change reconnect behavior unless the task specifically concerns
connection reliability.

A temporary Home Assistant failure must not require rebooting the ESP.

The firmware must recover automatically from:

- Home Assistant unavailable
- WebSocket disconnect
- authentication connection loss
- network interruption
- WiFi reconnect
- Home Assistant restart

Home Assistant instability must never permanently disable the display or
configuration Web UI.

---

## WebSocket Memory Safety

The project previously experienced ESP32-C3 crashes caused by memory
allocation inside the ArduinoWebsockets message path.

A decoded crash showed `std::bad_alloc` / `operator new()` activity from
`ArduinoWebsockets` message handling.

Therefore:

- Avoid unnecessary copies of WebSocket messages.
- Avoid large temporary `String` objects in callbacks.
- Avoid parsing large Home Assistant event payloads unless necessary.
- Prefer ArduinoJson filters when only a small subset of JSON is needed.
- Keep WebSocket callbacks short.
- Avoid additional heap allocation in high-frequency message paths.
- Do not subscribe to broad Home Assistant event streams when a targeted
  subscription is sufficient.

Memory stability is more important than convenience.

---

## Home Assistant REST

REST is used for initial state loading and weather data.

REST failures must be handled gracefully.

Requirements:

- use finite connection/read timeouts
- never block indefinitely waiting for Home Assistant
- malformed JSON must not crash or reboot the ESP
- unavailable entities must be tolerated
- failed REST requests must not prevent the Web UI from running

Do not repeatedly poll printer entities when WebSocket updates already
provide the required live state.

---

## Weather

Weather data is provided by Home Assistant.

Current behavior:

- current weather entity is configurable
- current conditions are obtained from Home Assistant
- daily forecast is obtained through Home Assistant
- idle screen displays current conditions, tomorrow and the following day
- weather refresh interval is configurable

Weather is secondary functionality.

Weather failure must never interfere with:

- printer monitoring
- WebSocket operation
- display mode switching
- Web UI
- WiFi recovery

Do not create a second persistent WebSocket connection solely for weather
unless explicitly requested.

---

## Display

The display currently works correctly and is considered visually
calibrated.

Preserve its appearance unless the task explicitly concerns the UI.

### Flicker-Free Rendering

The display uses dirty-region rendering.

This is intentional.

Do NOT reintroduce periodic full-screen redraws.

Do NOT call `fillScreen()` on every display refresh.

A complete screen redraw should normally happen only when changing major
display modes, such as:

- IDLE -> PRINTING
- PRINTING -> IDLE

Otherwise redraw only the region whose value changed.

Examples:

- clock: only when displayed time changes
- date: only when date changes
- progress: only when progress changes
- temperatures: only when values change
- layer: only when layer changes
- connection indicators: only when connection state changes
- weather: only after changed/new weather data

Avoid visible flicker.

---

## Fonts

Use the existing smooth GFX fonts.

Do not revert to the default pixelated Adafruit_GFX font for normal UI
text.

Maintain visual consistency with the current display.

---

## Printer Screen

The working printing screen must remain unchanged unless explicitly
requested.

It currently includes:

- progress bar
- percentage
- alternating ETA / Remaining display
- current / total layer
- speed mode
- Hotend icon and temperature
- Bed icon and temperature
- Chamber icon and temperature

The Hotend, Bed and Chamber icon positions have been manually adjusted for
the physical display and should not be changed as part of unrelated work.

---

## Idle Screen

The idle screen currently includes:

- common header
- printer idle state
- long date
- weather
- current conditions
- forecast
- Home Assistant connection status
- diagnostics

Do not restore the previous large-clock-only idle layout.

---

## Configuration

Runtime configuration is stored persistently using LittleFS.

Configuration must be versioned.

Always provide safe defaults for missing configuration values.

Firmware updates must tolerate an older or incomplete configuration file
where reasonably possible.

Invalid configuration must not cause a reboot loop.

The Web UI must remain accessible even if Home Assistant configuration is
invalid.

---

## Secrets

The Home Assistant Long-Lived Access Token is sensitive.

Requirements:

- never print the LLAT to Serial
- never expose the stored LLAT in HTML
- never include the LLAT in diagnostics
- never include the LLAT in logs
- never include the LLAT in Git commits
- never write the LLAT into README or AGENTS.md

When editing configuration through the Web UI:

- token field must be blank by default
- an empty submitted token means "keep the existing token"
- replace the stored token only when a new non-empty token is explicitly
  submitted

Avoid exposing WiFi passwords in logs or UI.

Repository review found a plaintext LLAT in `Config.cpp` in existing Git
history. The existing token must therefore be considered exposed and
rotated. Do not reproduce the token value in documentation, diagnostics,
logs, UI output, or future commits. Do not rewrite Git history unless the
user explicitly requests a separate history-cleanup operation.

---

## Web UI

The Web UI must operate independently from Home Assistant availability.

It must remain usable when:

- HA host is wrong
- HA is offline
- authentication fails
- weather fails
- WebSocket is disconnected

The current lightweight HTTP implementation was chosen to reduce flash
usage.

Do not replace it with the Arduino `WebServer` library unless there is a
strong reason and flash impact has been evaluated.

The previous `WebServer` implementation caused the firmware to exceed the
default ESP32-C3 application partition.

Keep the HTTP implementation simple and bounded.

Close client connections correctly.

Avoid long-running HTTP handlers.

---

## WiFi

WiFi loss must not require a manual reboot.

The existing WiFi recovery logic should be preserved.

Future captive-portal functionality must integrate with the existing
configuration architecture without breaking automatic WiFi recovery.

Do not add blocking WiFi loops to normal runtime operation.

---

## Reliability

The ESP is intended to run continuously.

Code must tolerate transient failures.

Avoid:

- infinite waits
- unbounded loops waiting for network services
- uncontrolled recursion
- large stack allocations
- unnecessary dynamic allocations
- repeated construction of large Strings
- blocking network calls without timeouts

Where practical, prefer state-machine/non-blocking behavior using
`millis()` over long `delay()` calls.

Short delays already required by stable existing code should not be
removed merely for stylistic reasons.

---

## RAM and Flash

Target hardware is memory constrained.

Current build uses the Huge APP partition.

Before introducing a large library, consider:

- flash impact
- heap impact
- dependency impact

Prefer existing linked functionality over adding another library for a
small feature.

Do not sacrifice reliability for small flash savings.

If flash becomes constrained, report measured build sizes before removing
features.

---

## Serial Diagnostics

Serial output is intentionally available in this display project for
diagnostics.

Do not print:

- LLAT
- WiFi password
- other secrets

Useful diagnostic information includes:

- boot count
- reset reason
- uptime
- WiFi state
- WebSocket state
- authentication state
- subscription state
- reconnect count
- trigger count
- free heap
- minimum free heap

Do not add high-frequency Serial logging in normal operation unless needed
for diagnosing a specific problem.

---

## Error Handling

Never silently ignore important failures.

For recoverable failures:

1. record/report the problem where useful
2. preserve the rest of the application
3. retry with appropriate timing/backoff
4. recover automatically when possible

Do not reboot the ESP merely because:

- Home Assistant is unavailable
- weather retrieval failed
- a REST request timed out
- WebSocket disconnected
- configuration contains a recoverable error

---

## Change Policy

Before making a significant change:

1. inspect the existing implementation
2. identify the smallest set of files that need modification
3. preserve unrelated behavior
4. avoid broad cleanup/refactoring
5. explain important architectural changes before implementing them

For small obvious changes, implement them directly without unnecessary
redesign.

After making changes:

1. summarize files modified
2. summarize behavior changes
3. mention assumptions
4. mention known risks
5. report build results if a build was performed

Do not claim a build succeeded unless it was actually run successfully.

---

## Git

Git is used as the safety net for this project.

Known good baselines include:

- `working-baseline-after-refactor`
- `working-baseline-with-weather-webui`

Do not rewrite or delete these tags.

Before a large or risky change, verify that the working tree is clean.

Do not automatically reset, revert, discard, or overwrite user changes.

Do not create commits or tags unless requested, except when the user
explicitly asks for an automatic checkpoint workflow.

Never use destructive Git commands without explicit approval.

---

## Current Known-Good Baseline

The user reported that the baseline tagged
`working-baseline-with-weather-webui` compiled, flashed, and appeared to
function correctly on the target hardware. Git alone cannot verify runtime
behavior.

The user-reported behavior included:

- compile
- flash successfully
- display printer information
- switch between idle and printing modes
- receive Home Assistant trigger events
- reconnect to Home Assistant
- load weather
- display weather on the idle screen
- provide the configuration Web UI
- use persistent configuration
- operate with flicker-free display updates

Treat regressions relative to this baseline as bugs unless the requested
change intentionally modifies that behavior.

---

## Testing Expectations

After changes that affect core functionality, verify as applicable:

### Boot
- normal boot succeeds
- configuration loads
- display initializes
- WiFi connects

### Home Assistant
- REST initial state loading works
- WebSocket connects
- authentication succeeds
- trigger subscription succeeds
- printer triggers arrive

### Printer state
- boot while printer is idle
- boot while printer is printing
- idle -> printing transition
- printing -> idle transition

### Recovery
- temporary HA disconnect
- HA reconnect
- temporary WiFi disconnect
- WiFi reconnect

### Display
- no unnecessary full-screen redraw
- no visible periodic flicker
- temperatures update correctly
- progress updates correctly
- ETA/Remaining alternates correctly
- weather updates without disturbing printer screen

### Web UI
- status page loads
- configuration page loads
- configuration survives reboot
- LLAT is not exposed
- empty LLAT submission preserves existing token
- Web UI remains available while HA is offline

Do not assume these tests passed merely because compilation succeeded.

---

## Future Development

Likely future work includes:

- WiFi captive portal / first-run configuration
- additional configurable parameters
- improved Web UI
- further diagnostics
- long-term stability improvements

Implement future features incrementally.

Do not combine unrelated feature additions into one large change.

---

## Decision Priority

When requirements conflict, use this priority:

1. Stability
2. Correct printer monitoring
3. Automatic HA/WiFi recovery
4. Memory safety
5. Display correctness / flicker-free behavior
6. Configuration accessibility
7. Weather
8. Convenience / code elegance

A simpler implementation that has already proven stable is preferable to
a theoretically cleaner implementation that introduces additional risk.
