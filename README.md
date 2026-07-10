# ESP2Serial

An ESP32 acts as a transparent USB-to-UART bridge so that a host machine (macOS/Linux) can serially flash and communicate with an ESP8266 target device (Sonoff iFan04 / ESP8285).

After the first serial flash the target firmware connects to WiFi, exposes an OTA endpoint and a web UI, and listens on its UART RX pin for remote-control signals.

## Repository layout

```
ESP2Serial/
├── src/main.cpp            # ESP32 bridge firmware (transparent UART passthrough)
├── platformio.ini          # ESP32 build environment
└── esp8266-target/
    ├── src/main.cpp        # ESP8266 target firmware
    ├── platformio.ini      # ESP8266 build environments (serial + OTA)
    ├── extra_scripts/
    │   └── load_env.py     # Pre-build script that injects .env into build flags
    ├── .env.example        # Template – copy to .env and fill in your values
    └── README.md           # ESP8266-specific detail (German)
```

## Hardware requirements

| Part | Role |
|------|------|
| ESP32 DevKit (az-delivery-devkit-v4) | Bridge – connects to the host via USB, forwards UART to the target |
| Sonoff iFan04 (ESP8285) | Target – receives firmware and remote-control signals |

### Wiring

```
Host USB ──► ESP32 (UART0 USB)
                │
                ├─ TX (GPIO17) ──► RX (GPIO3)  ESP8266
                └─ RX (GPIO16) ◄── TX (GPIO1)  ESP8266
```

> **Note:** Only RX/TX are wired. GPIO0 and RESET/EN of the ESP8266 are **not** connected, so the ESP8266 must be put into ROM bootloader mode **manually** (hold GPIO0 low while powering on or pressing reset) for the first serial flash.

## Quick start

### 1. Flash the ESP32 bridge

```bash
# From repo root
platformio run -t upload
```

### 2. Configure the ESP8266 target

```bash
cd esp8266-target
cp .env.example .env
# Edit .env and set at minimum:
#   WIFI_SSID, WIFI_PASSWORD, OTA_HOSTNAME
```

### 3. First serial flash of the ESP8266

Put the ESP8266 into ROM bootloader mode (GPIO0 low, then reset), then:

```bash
cd esp8266-target
platformio run -e ifan04_serial -t upload --upload-port /dev/cu.SLAB_USBtoUART
```

Adjust `--upload-port` to your OS:

| OS | Typical port |
|----|-------------|
| macOS | `/dev/cu.SLAB_USBtoUART` |
| Linux | `/dev/ttyUSB0` |
| Windows | `COM3` (check Device Manager) |

### 4. Monitor serial output

```bash
cd esp8266-target
platformio device monitor -b 9600 --port /dev/cu.SLAB_USBtoUART
```

The firmware prints the assigned IP address on boot.

### 5. Subsequent OTA updates

```bash
cd esp8266-target
# Option A – use OTA_TARGET_IP from .env
platformio run -e ifan04_ota -t upload

# Option B – override IP inline
ESP8266_OTA_TARGET=192.168.1.123 platformio run -e ifan04_ota -t upload
```

## Web UI

Once the ESP8266 is running and on the network:

- **WiFi mode:** `http://<ip>/` or `http://<OTA_HOSTNAME>.local/`
- **Fallback AP mode:** `http://192.168.4.1/`

The web UI shows the current network mode and IP, lets you update WiFi / OTA settings (persisted in flash), and displays the last 24 received remote-control frames as HEX and printable text.

## .env reference

| Key | Required | Default | Description |
|-----|----------|---------|-------------|
| `WIFI_SSID` | ✓ | — | WiFi network to connect to |
| `WIFI_PASSWORD` | ✓ | — | WiFi password |
| `OTA_HOSTNAME` | ✓ | — | mDNS hostname for OTA and web UI |
| `OTA_TARGET_IP` | — | — | IP used by `ifan04_ota` upload |
| `FALLBACK_AP_SSID` | — | `ifan04` | Prefix for the fallback AP name (chip ID appended) |
| `FALLBACK_AP_PASSWORD` | — | open | AP password; must be empty or ≥ 8 characters |

The `.env` file is excluded from version control. Saved settings from the web UI override `.env` values until cleared.

## Known issues

### 1. Baud rate mismatch breaks serial flash *(was HIGH – fixed)*

`esp8266-target/platformio.ini` previously set `upload_speed = 57600` while the ESP32 bridge runs its UART2 at a fixed 115200 baud. esptool first syncs at 115200, then issues a *Change Baud* command to switch to 57600. The ESP8266 ROM honours that command and switches to 57600, but the bridge stays at 115200 – the link breaks and the flash fails.

**Fix applied:** `upload_speed` changed to `115200` to keep the bridge and target in sync throughout the flash.

### 2. Serial.begin() called after loadPersistedConfig() *(was MEDIUM – fixed)*

`setup()` in `esp8266-target/src/main.cpp` called `Serial.begin()` *after* `LittleFS.begin()` and `loadPersistedConfig()`. Any error printed by `loadPersistedConfig()` (e.g. "Failed to open persisted config") was silently discarded because the UART had not yet been initialised.

**Fix applied:** `Serial.begin()` is now the first call in `setup()`.

### 3. LittleFS.begin() return value ignored *(was LOW – fixed)*

If the flash filesystem is not yet formatted (e.g. on a freshly programmed board), `LittleFS.begin()` returns `false` and all subsequent filesystem calls fail silently. The persisted config is never loaded and no error is reported.

**Fix applied:** The return value is now checked; on failure `LittleFS.format()` is called once and the filesystem is mounted again.

### 4. OTA upload port is the STA IP – does not work in fallback AP mode *(known limitation)*

`OTA_TARGET_IP` in `.env` is the device's IP on the home network. If the ESP8266 cannot join that network and starts its fallback AP instead, its IP is `192.168.4.1`. An OTA upload pointed at the STA IP will time out.

**Workaround:** Set `ESP8266_OTA_TARGET=192.168.4.1` when the device is in AP mode.

### 5. Hard-coded macOS USB port *(known limitation)*

`platformio.ini` (root) contains `upload_port = /dev/cu.SLAB_USBtoUART` which is macOS-specific. Linux and Windows users must override this with `--upload-port` on the command line or by editing the file.

### 6. ESP8285 board profile uses 1 MB flash *(known limitation)*

PlatformIO's built-in `esp8285` board definition assumes 1 MB flash. Some iFan04 revisions ship with 2 MB. Using the wrong profile wastes half the available flash and may cause partition-layout issues. Add `board_build.flash_size = 2MB` to `esp8266-target/platformio.ini` if your hardware has 2 MB.
