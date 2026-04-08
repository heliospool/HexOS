![GitHub Downloads (all assets, all releases)](https://img.shields.io/github/downloads/heliospool/HexOS/total?include_prereleases)
![GitHub Release](https://img.shields.io/github/v/release/heliospool/HexOS?include_prereleases)
![GitHub commit activity](https://img.shields.io/github/commit-activity/t/heliospool/HexOS)

# HexOS

HexOS is open-source firmware for Bitcoin miners, built and maintained for [HeliosPool](https://heliospool.com) by [Z3r0XG](https://github.com/Z3r0XG). It is a fork of [bitaxeorg/ESP-Miner](https://github.com/bitaxeorg/ESP-Miner), tracking upstream closely while adding hardware support, pool integration, and UI improvements.

For pre-built images ready to flash, see the [latest release](https://github.com/heliospool/HexOS/releases/latest).

---

## Supported Hardware

HexOS supports all upstream ESP-Miner hardware plus the following additional boards:

### GekkoAxe GT

| Parameter | Value |
|---|---|
| Board version | `gekko-800` |
| ASICs | 2× BM1370 |
| Device family | `GekkoAxe-GT` |
| Voltage regulator | TPS546 (multi-phase) |
| Fan controller | EMC2103 |
| MCU | ESP32-S3-WROOM-1 N16R8 (16 MB Flash, 8 MB Octal SPI PSRAM) |
| Input voltage | 12 V |
| Default ASIC frequency | 600 MHz |
| Default ASIC voltage | 1100 mV |

### GekkoAxe Gamma 5 V

| Parameter | Value |
|---|---|
| Board version | `gekko-601` |
| ASICs | 1× BM1370 |
| Device family | `GekkoAxe-γ` |
| Voltage regulator | TPS546 |
| Fan controller | EMC2101 |
| MCU | ESP32-S3-WROOM-1 N16R8 (16 MB Flash, 8 MB Octal SPI PSRAM) |
| Input voltage | 5 V |
| Default ASIC frequency | 525 MHz |
| Default ASIC voltage | 1150 mV |

### GekkoAxe Gamma 12 V

| Parameter | Value |
|---|---|
| Board version | `gekko-601-12` |
| ASICs | 1× BM1370 |
| Device family | `GekkoAxe-γ` |
| Voltage regulator | TPS546 |
| Fan controller | EMC2101 |
| MCU | ESP32-S3-WROOM-1 N16R8 (16 MB Flash, 8 MB Octal SPI PSRAM) |
| Input voltage | 12 V |
| Default ASIC frequency | 525 MHz |
| Default ASIC voltage | 1150 mV |

> **Note:** `gekko-601` (5 V) uses the upstream ESP-Miner `601` configuration unmodified. `gekko-601-12` (12 V) adds support for 12 V input and should be considered experimental.

---

## Changes vs upstream ESP-Miner

### Helios Pool integration

- **Account stats banner** — home dashboard and swarm view show live hashrate, workers, and earnings from the Helios Pool API
- **Default pool** — primary pool defaults to `btc.heliospool.com`, fallback to `bch.heliospool.com`

### Hardware & board support

- **GekkoAxe hardware support** — dedicated device family entries for GekkoAxe-GT and GekkoAxe-γ with correct regulator config, fan controller, and board-specific power parameters
- **Bitaxe Gamma Turbo (GT) 800 device support** — support put back in for GT-800 devices (only 801 boards were supported)
- **Self-test power ceiling corrected** — `gekko-800` `power_consumption_target` fixed from 12 W to 36 W (dormant upstream copy-paste bug; only affects devices with `selftest=1`)
- **Fan controller improvements** — auto fan continues working if any individual ASIC temperature sensor stops reporting; fails safe to 100% if all sensors are lost; more aggressive response to rising temperatures with gradual spin-down to prevent oscillation

### Branding & identity

- **HexOS branding** — UI title, page labels, topbar, and favicon reflect HexOS
- **Boot logo screens** — HexOS gothic logo on first boot splash; HeliosPool logo on second boot splash
- **WiFi AP renamed** — setup-mode access point shows as `HexOS_XXYY` instead of `Bitaxe_XXYY`

### OTA

- **OTA updates point to this repo** — the in-UI update checker resolves releases from `heliospool/HexOS`
- **OTA file naming** — firmware OTA expects `hexos-firmware-*.bin`; web OTA expects `hexos-www-*.bin`

### Web UI & dashboard

- **Expert Mode toggle** — Settings page toggle replaces the `?oc` URL parameter for enabling custom ASIC frequency and voltage; persists to NVS
- **Board temperature** — EMC fan controller internal die temperature exposed as `boardTemp` in `/api/system/info`, shown as a progress bar in the Heat card, and available as a chart series
- **Jobs counter** — stratum work received since last pool connection exposed as `workReceived` in `/api/system/info`, shown in the Shares card
- **Last submitted share diff** — live `lastSubmittedDiff` stat in `/api/system/info` and selectable as a chart series

### Mining & protocol

- **BCH coinbase decoding** — "Decode Coinbase Tx" is now a dropdown: Auto / BTC / BCH / Disabled. Auto detects the coin from your payout address. BCH outputs decode to correct CashAddr

### Configuration & tuning

- **Stratum connection tuning** — `strat_retry`, `strat_crit_rty`, and `strat_timeout` configurable via CVS file; see [Stratum — connection tuning](./doc/configuration.md#stratum--connection-tuning)
- **Self-test parameter tuning** — `st_difficulty`, `st_pwr_margin`, `st_vcore_min`, and `st_vcore_max` configurable via CVS file; see [Self-test parameters](./doc/configuration.md#self-test-parameters)
- **Danger zone NVS keys** — `dangerzone=1` in the CVS file unlocks thermal thresholds, fan PID gains, and TPS546 VIN limits without editing source code; see [Danger zone](./doc/configuration.md#danger-zone)

---

## Building from source

### Prerequisites

- [ESP-IDF v5.5](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/get-started/) targeting `esp32s3`
- Node.js ≥ 22 and npm (for the Angular web UI)
- Linux or macOS recommended

### Quick setup

```bash
git clone https://github.com/heliospool/HexOS.git
cd HexOS

# Install ESP-IDF v5.5
git clone --branch v5.5 --depth 1 --recursive https://github.com/espressif/esp-idf.git ~/esp/idf
~/esp/idf/install.sh esp32s3
```

### Build

```bash
bash build_release.sh
```

This sources ESP-IDF, builds the full firmware + Angular web UI, and produces per-board factory images plus shared firmware/www artifacts in `releases/{VERSION}/`:

| File | Use |
|---|---|
| `HexOS-{VERSION}-factory-{BOARD}.bin` | Full 16 MB factory image for each board, flash at `0x0` |
| `HexOS-{VERSION}-firmware.bin` | Firmware only, for OTA Firmware update (all boards) |
| `HexOS-{VERSION}-www.bin` | Web UI only, for OTA Web update (all boards) |
| `config-{BOARD}.cvs` | NVS config used to build each factory image |

---

## Board configuration

CVS files define the initial NVS configuration baked into factory images. Keys absent from the file use firmware defaults. Pool, WiFi, fan, and ASIC settings can also be changed through the web UI after flashing.

The file format is documented in `config.cvs.example`.

### Required keys

| Key | Type | Default | Notes |
|---|---|---|---|
| `boardversion` | string | — | e.g. `gekko-800`, `601`, `201` |
| `asicfrequency_f` | string | — | MHz |
| `asicvoltage` | u16 | — | mV |
| `stratumurl` | string | `btc.heliospool.com` | Primary pool host |
| `stratumport` | u16 | `3333` | Primary pool port |
| `stratumuser` | string | — | Payout address and worker name |
| `fbstratumurl` | string | `bch.heliospool.com` | Fallback pool host |
| `fbstratumport` | u16 | `3333` | Fallback pool port |
| `fbstratumuser` | string | — | Fallback payout address and worker name |

For all available NVS keys, see [`doc/configuration.md`](./doc/configuration.md).

---

## Flashing

Pre-built images are available on the [releases page](https://github.com/heliospool/HexOS/releases/latest).

### Factory flash (first-time or full reset)

**Option A — bitaxetool**

> bitaxetool v0.6.1 is required (locked to esptool v4.9.0). esptool v5.x is not compatible.

```bash
pip install bitaxetool==0.6.1
bitaxetool --config ./config-{BOARD}.cvs --firmware ./HexOS-{VERSION}-factory-{BOARD}.bin
```

**Option B — esptool**

```bash
esptool.py --chip esp32s3 -b 921600 --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0x0 HexOS-{VERSION}-factory-{BOARD}.bin
```

### OTA update (device already running HexOS)

Navigate to your device's web UI → **Settings** → **Updates**.

- **Firmware update**: upload `HexOS-{VERSION}-firmware.bin`
- **Web UI update**: upload `HexOS-{VERSION}-www.bin`

The in-UI update checker automatically compares against the latest release on this repository.

---

## API

The web server on port 80 exposes a REST API. Full spec: [`main/http_server/openapi.yaml`](./main/http_server/openapi.yaml).

**GET**
- `/api/system/info` — system information (hashrate, temps, uptime, pool, `lastSubmittedDiff`, `workReceived`, `boardTemp`, etc.)
- `/api/system/asic` — ASIC settings
- `/api/system/statistics?columns=...` — historical stats ring buffer (720 entries)
- `/api/system/statistics/dashboard` — dashboard stats
- `/api/system/wifi/scan` — available Wi-Fi networks

**POST**
- `/api/system/restart` — restart the device
- `/api/system/identify` — flash LEDs / beep
- `/api/system/OTA` — upload firmware binary
- `/api/system/OTAWWW` — upload web UI binary

**PATCH**
- `/api/system` — update settings (pool, Wi-Fi, fan speed, voltage, frequency, etc.)

```bash
# Current system info
curl http://<device-ip>/api/system/info

# OTA firmware update
curl -X POST \
     -H "Content-Type: application/octet-stream" \
     --data-binary "@HexOS-{VERSION}-firmware.bin" \
     http://<device-ip>/api/system/OTA
```

---

## Credits

HexOS is built on [ESP-Miner](https://github.com/bitaxeorg/ESP-Miner) by the Bitaxe community. All upstream contributors retain their credit.

## Attributions

The display font is Portfolio 6x8 from https://int10h.org/oldschool-pc-fonts/ by VileR.

