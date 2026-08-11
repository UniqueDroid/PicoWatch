<p align="center"><img src="extras/picowatch_logo.svg" alt="PicoWatch logo" width="200"></p>

# PicoWatch — E-Paper Watch Firmware

PicoWatch is Jan's personal fork of [SQFMI's Watchy](https://github.com/sqfmi/Watchy), an
open source E-Paper watch, built for a custom PCB V3 (ESP32-S3) with a large set of
features on top of the original project.

For hardware purchase, case/accessories, and the original getting-started guide, see the
upstream project at [watchy.sqfmi.com](https://watchy.sqfmi.com).

## Features

### Watchfaces

Eight built-in designs, each its own example sketch under `examples/WatchFaces/`: Basic
(default 7-segment), 7-Segment, DOS, Mario, MacPaint, Pokemon, StarryHorizon, and Tetris.
The `AllFaces` sketch bundles all eight into one firmware with a picker screen (Settings
menu's "Change Watchface").

### Main menu

- **Change Watchface** — pick a design (AllFaces build only)
- **Stopwatch**
- **Steps (7 Days)** — rolling 7-day step history, captured automatically at midnight
- **Alarm** — on/off, hour/minute
- **Weather (5 Days)** — OpenWeatherMap-based current conditions + 5-day forecast
- **Games** — Snake, Pong, Tetris, and Flappy, playable right on the watch (see below)
- **Settings** — see below

### Games

Built for a 4-button watch (Up/Down/Menu/Back, no d-pad), so each game gets its own
control scheme instead of assuming arrow keys:

- **Snake** — steers relative to its own heading (Up = turn left, Down = turn right)
- **Pong** — solo "wall pong": a right-side paddle (Up/Down) keeps a bouncing ball in play
- **Tetris** — Up/Down shift the piece left/right, Menu rotates, gravity-only drop
- **Flappy** — Up flaps

All four: Menu pauses (Snake/Pong/Flappy), Back exits to the Games menu, score shown on
game over.

### Settings

- **About** — firmware/library version, battery voltage, uptime, WiFi status
- **Vibrate Motor** — test buzz
- **Show Accelerometer** — live BMA423 orientation debug screen
- **Time** — grouped submenu:
  - Set Time
  - Sync NTP
  - Set Timezone (15-minute steps, GMT-12:00 to +14:00)
  - Vibrate Window — on/off + from-hour/to-hour range for the hourly on-the-tick vibration,
    so it doesn't also buzz all night
- **WiFi** — connect via WiFiManager, or open the setup portal; see Web UI below
- **Set City** — 7-digit OpenWeatherMap city ID picker
- **Online Update** — check GitHub releases and flash the latest one over WiFi
  (SHA-256 verified against the release asset before installing)
- **Button Settings** — swap Menu/Back, and assign short/long-press actions to
  Up/Down while on the watchface (Settings, Change Watchface, Weather, Stopwatch, Alarm,
  or nothing)
- **Font Size** — Small/Default/Big for the menu and Settings list
- **Language** — switch the UI language on-device, no reflash needed (currently
  English/Deutsch; see `src/languages/localization_template.h` for adding another)

### Web UI

Once connected to WiFi (Settings → WiFi), the watch serves a small password-protected web
menu at its IP address:

- Real login gate (session persists for an hour, tied to your IP)
- **Online Update** — same GitHub-release check/flash as the on-device menu
- **File Update** — upload a `.bin` directly from the browser
- **Change Password**
- **Config Erase** — wipe all saved settings and WiFi credentials, then reboot
- Stock WiFiManager pages (Configure WiFi, Info, Restart) alongside the above

The web portal (and its patched WiFiManager fork, see `third_party/wifimanager-patched/`)
is styled to match Jan's other ESP32 dashboard projects (dark navy/cyan theme).

### Firmware updates

Two independent paths, both SHA-256 verified before installing:

- **Online Update** — checks this repo's latest GitHub release, downloads and flashes it
  (on-device menu or web UI)
- **File Update** — manual `.bin` upload via the web UI
- **BLE OTA** — also available (see `src/Hardware/BLE.cpp`)

### Power

- CPU clock drops to 80 MHz while awake (the floor for stable WiFi/BT) — most of the
  awake time is button-polling or I/O waits anyway, not CPU-bound
- Night wake-interval reduction: between 23:00–05:00 (configurable in `config.h`) the
  watch wakes every 45 minutes instead of every minute, without missing the midnight
  steps-history capture or a scheduled alarm

## Hardware

Built and tested for **PCB V3 (ESP32-S3)**. See `src/config.h` for pin mappings and
`src/Hardware/` for the display/BLE/accelerometer/RTC drivers.

## Building

Arduino library layout (`library.json`/`library.properties` + `src/` +
`examples/WatchFaces/*.ino`) — build with **arduino-cli**, not PlatformIO:

```
arduino-cli compile --library . --fqbn "esp32:esp32:esp32s3:FlashSize=8M,PartitionScheme=default_8MB,UploadSpeed=921600,DebugLevel=none" examples/WatchFaces/AllFaces
```

The web UI's native login gate needs the patched WiFiManager in
`third_party/wifimanager-patched/` copied over the installed WiFiManager library before
building (see that folder's origin in `pfsense-status-esp32`'s own CI for the same step).

## Hardware mods

`Frontlight-Conversion/` documents a MOSFET/I²C-driven frontlight mod for the display
(v3 and v2/USB-C-clone variants) — reference documentation only, not built into the
firmware here.

### Have Fun! :)
