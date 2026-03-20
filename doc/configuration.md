# Board configuration reference

CVS files define the initial NVS configuration baked into factory images. Keys absent from the file use firmware defaults. Pool, WiFi, fan, and ASIC settings can also be changed through the web UI after flashing.

The file format is documented in [`config.cvs.example`](../config.cvs.example).

**Contents:** [NVS keys](#nvs-keys) — [Board](#board) · [Network](#network) · [Stratum primary](#stratum--primary-pool) · [Stratum fallback](#stratum--fallback-pool) · [Stratum tuning](#stratum--connection-tuning) · [ASIC](#asic) · [Self-test](#self-test-parameters) · [Fan & thermal](#fan-and-thermal) · [Display](#display) · [Hardware peripherals](#hardware-peripherals) · [System](#system) · [**Danger zone**](#danger-zone) | [Firmware constants](#firmware-constants)

---

## NVS keys

`—` in the Default column means the key has no valid firmware default and **must** be present in the CVS file.

---

### Board

| Key | Type | Default | Unit | Options | Notes |
|---|---|---|---|---|---|
| `boardversion` | string | — | | | Board identifier string; valid values and hardware mappings are defined in `device_config.c` |

---

### Network

| Key | Type | Default | Unit | Options | Notes |
|---|---|---|---|---|---|
| `hostname` | string | `GekkoAxe` | | | mDNS/DHCP hostname |
| `wifissid` | string | *(empty)* | | | WiFi network name |
| `wifipass` | string | *(empty)* | | | WiFi password |

---

### Stratum — primary pool

| Key | Type | Default | Unit | Options | Notes |
|---|---|---|---|---|---|
| `stratumuser` | string | — | | | Worker identity string for the primary pool |
| `stratumurl` | string | `btc.heliospool.com` | | | |
| `stratumport` | u16 | `3333` | | | |
| `stratumpass` | string | `x` | | | Most pools ignore this |
| `stratumdiff` | u16 | `1000` | | | Suggested starting difficulty |
| `stratumtls` | u16 | `0` | | `0` = off<br>`1` = bundled CA<br>`2` = custom cert | |
| `stratumcert` | string | *(empty)* | | | Custom CA cert PEM; only used when `stratumtls` = `2` |
| `stratumxnsub` | bool | `0` | | | Extranonce subscribe |
| `stratumdecode2` | u16 | `3` | | `0` = disabled<br>`1` = BTC<br>`2` = BCH<br>`3` = auto | Coinbase decode mode |

---

### Stratum — fallback pool

| Key | Type | Default | Unit | Options | Notes |
|---|---|---|---|---|---|
| `fbstratumuser` | string | — | | | Worker identity string for the fallback pool |
| `fbstratumurl` | string | `bch.heliospool.com` | | | |
| `fbstratumport` | u16 | `3333` | | | |
| `fbstratumpass` | string | `x` | | | |
| `fbstratumdiff` | u16 | `1000` | | | |
| `fbstratumtls` | u16 | `0` | | `0` = off<br>`1` = bundled CA<br>`2` = custom cert | |
| `fbstratumcert` | string | *(empty)* | | | Fallback custom CA cert |
| `stratumfbxnsub` | bool | `0` | | | Extranonce subscribe |
| `fbstratumdec2` | u16 | `3` | | `0` = disabled<br>`1` = BTC<br>`2` = BCH<br>`3` = auto | Coinbase decode mode |
| `usefbstartum` | bool | `0` | | | Always use fallback pool |

---

### Stratum — connection tuning

Set via CVS file only — not accessible via the REST API or Web UI. Take effect without `dangerzone=1`.

| Key | Type | Default | Unit | Notes |
|---|---|---|---|---|
| `strat_retry` | u16 | `3` | | Connection retry attempts before switching to fallback pool |
| `strat_crit_rty` | u16 | `5` | | Retry attempts on critical transport errors before `esp_restart()` |
| `strat_timeout` | u16 | `5000` | ms | Socket connect and receive timeout |

---

### ASIC

| Key | Type | Default | Unit | Options | Notes |
|---|---|---|---|---|---|
| `asicfrequency_f` | float | — | MHz | | ASIC clock frequency |
| `asicvoltage` | u16 | — | mV | | ASIC core voltage |
| `oc_enabled` | bool | `0` | | | Overclock mode |

---

### Self-test parameters

Set via CVS file only — not accessible via the REST API or Web UI. Take effect without `dangerzone=1`. The `selftest` key in [Hardware peripherals](#hardware-peripherals) must be `1` for these to have any effect.

| Key | Type | Default | Unit | Notes |
|---|---|---|---|---|
| `st_difficulty` | u16 | `16` | | Share difficulty used during hash self-test |
| `st_pwr_margin` | u16 | `3` | W (±) | Acceptable deviation from target power consumption during self-test |
| `st_vcore_min` | u16 | `1000` | mV | Minimum acceptable core voltage during self-test. Must be less than `st_vcore_max`. |
| `st_vcore_max` | u16 | `1300` | mV | Maximum acceptable core voltage during self-test. Must be greater than `st_vcore_min`. |

---

### Fan and thermal

| Key | Type | Default | Unit | Options | Notes |
|---|---|---|---|---|---|
| `autofanspeed` | bool | `1` | | | `0` = manual speed · `1` = automatic |
| `manualfanspeed` | u16 | `100` | % | | Fan speed in manual mode |
| `minfanspeed` | u16 | `25` | % | | Minimum fan speed in auto mode |
| `temptarget` | u16 | `60` | °C | | Target ASIC temperature (35–66) |
| `overheat_mode` | bool | `0` | | | Set by firmware during thermal events; set to `0` to clear |

---

### Display

| Key | Type | Default | Unit | Options | Notes |
|---|---|---|---|---|---|
| `display` | string | per boardversion | | `NONE`<br>`SSD1306 (128x32)`<br>`SSD1309 (128x64)`<br>`SH1107 (64x128)`<br>`SH1107 (128x128)` | |
| `rotation` | u16 | `0` | ° | `0`<br>`90`<br>`180`<br>`270` | Screen rotation |
| `invertscreen` | bool | `0` | | | Invert display colours |
| `displayOffset` | u16 | per boardversion | | | Display offset register value (SH1107 screens) |
| `displayTimeout` | i32 | `-1` | s | | Time before screen off; `-1` = never |

---

### Hardware peripherals

Derived automatically from `boardversion` — do not need to be set in most cases.

| Key | Type | Default | Unit | Notes |
|---|---|---|---|---|
| `TPS546` | bool | per boardversion | | TPS546 buck converter present (voltage regulation via I²C) |
| `DS4432U` | bool | per boardversion | | DS4432U current DAC present (voltage control on boards without TPS546) |
| `INA260` | bool | per boardversion | | INA260 power monitor present (current/voltage/power sensing) |
| `EMC2101` | bool | per boardversion | | EMC2101 fan controller / temperature sensor present |
| `EMC2103` | bool | per boardversion | | EMC2103 fan controller / temperature sensor present |
| `EMC2302` | bool | per boardversion | | EMC2302 dual fan controller present |
| `TMP1075` | bool | per boardversion | | TMP1075 external temperature sensor present |
| `emc_int_temp` | bool | per boardversion | | Use the EMC controller's internal temperature diode instead of external |
| `emc_ideality_f` | u16 | per boardversion | | EMC ideality factor register value (fine-tunes remote diode temperature accuracy) |
| `emc_beta_comp` | u16 | per boardversion | | EMC beta compensation register value (transistor beta correction for remote diode) |
| `temp_offset` | i32 | `0` | °C | Temperature offset applied to reported ASIC temp (can be negative) |
| `plug_sense` | bool | per boardversion | | Barrel plug sense pin present (detects whether DC input is connected) |
| `asic_enable` | bool | per boardversion | | ASIC power enable pin present (`POWER_EN` line to core supply) |
| `power_cons_tgt` | u16 | per boardversion | W | Power budget; firmware throttles to stay within this limit |
| `selftest` | bool | `0` | | Run hash/power self-test on boot |

---

### System

| Key | Type | Default | Unit | Notes |
|---|---|---|---|---|
| `statsFrequency` | u16 | per boardversion | s | Statistics ring buffer sampling interval |
| `swarmconfig` | string | *(empty)* | | Swarm/mesh configuration JSON blob |
| `themescheme` | string | per boardversion | | UI theme scheme name |
| `themecolors` | string | per boardversion | | UI theme colour overrides |

---

### Danger zone

These keys are **gated** by the `dangerzone` key. When `dangerzone` is `0` (the default), all keys in this section are ignored at boot — the firmware uses the built-in defaults regardless of any stored NVS value. Set `dangerzone` to `1` in the CVS file and reflash to enable custom values.

No key in this section is accessible via the REST API or Web UI. Changes require a full reflash with an updated CVS file.

**Warning:** These constants control thermal shutdown, power reduction, and input voltage thresholds. Incorrect values can cause overheating, hardware damage, or voltage regulator failure. Only modify them if you understand the implications.

| Key | Type | Default | Unit | Notes |
|---|---|---|---|---|
| `dangerzone` | bool | `0` | | Gate key. Must be `1` for any other key in this section to take effect |
| `pid_p` | float | `15.0` | | Fan PID proportional gain |
| `pid_i` | float | `2.0` | | Fan PID integral gain |
| `pid_d` | float | `5.0` | | Fan PID derivative gain |
| `fan_dec_rate` | float | `1.0` | % / cycle | Maximum fan speed decrease per 100 ms control cycle |
| `throttle_temp` | float | `75.0` | °C | ASIC temperature that triggers a full power-down and cooling cycle. Must be greater than `safe_temp`. |
| `safe_temp` | float | `45.0` | °C | Temperature the ASIC must fall below before reinitialisation after an overheat event. Must be less than `throttle_temp` — if `safe_temp` ≥ `throttle_temp` the recovery loop will cycle indefinitely. |
| `vr_thr_temp` | float | `105.0` | °C | VR (TPS546) temperature that independently triggers the same power-down and cooling cycle. Independent of `throttle_temp`; whichever threshold is crossed first triggers shutdown. |
| `asic_reduction` | float | `100.0` | mV / MHz | Step-down applied to both core voltage (mV) and clock frequency (MHz) when reinitialising after an overheat event. Applied once per overheat; repeated overheats reduce further each time. |
| `vin_on` | float | `0` | V | Minimum input voltage to enable the TPS546 regulator. `0` = use boardversion default. Only consumed on TPS546-equipped boards. |
| `vin_off` | float | `0` | V | Input voltage below which the TPS546 regulator shuts off. `0` = use boardversion default. |
| `vin_ov_fault` | float | `0` | V | TPS546 overvoltage fault threshold. `0` = use boardversion default. |

---

## Firmware constants

These values are hardcoded in firmware source and are **not** NVS-exposed. To change them, edit the source file and rebuild firmware.

Several constants that were previously hardcoded have been moved to NVS and are now configurable via CVS file: thermal thresholds and fan PID gains (see [Danger zone](#danger-zone)), TPS546 VIN limits (see [Danger zone](#danger-zone)), stratum connection parameters (see [Stratum — connection tuning](#stratum--connection-tuning)), and self-test parameters (see [Self-test parameters](#self-test-parameters)).

---

### TPS546 power regulator (`main/power/vcore.c`)

Values are set per board family in `get_tps546_config()`. See the source for per-family values.

| Constant | Type | Value | Unit | Notes |
|---|---|---|---|---|
| `IOUT_OC_WARN_LIMIT` | float | per boardversion | A | Overcurrent warning threshold; triggers a status flag |
| `IOUT_OC_FAULT_LIMIT` | float | per boardversion | A | Overcurrent fault threshold; triggers latch-off |
| `VIN_ON` | float | per boardversion | V | Input voltage enable threshold; overridable via NVS `vin_on` (danger-zone gated) |
| `VIN_OFF` | float | per boardversion | V | Input voltage shutoff threshold; overridable via NVS `vin_off` (danger-zone gated) |
| `VIN_UV_WARN_LIMIT` | float | per boardversion | V | Input undervoltage warning threshold; does not latch off |
| `VIN_OV_FAULT_LIMIT` | float | per boardversion | V | Input overvoltage fault threshold; overridable via NVS `vin_ov_fault` (danger-zone gated) |
| `VOUT_COMMAND` | float | per boardversion | V | Initial output voltage setpoint; overridden at runtime by ASIC init |
| `VOUT_MIN` | float | per boardversion | V | Output voltage lower clamp |
| `VOUT_MAX` | float | per boardversion | V | Output voltage upper clamp |
| `SCALE_LOOP` | float | per boardversion | | Compensation loop scaling factor; set by PCB feedback resistor values |
| `STACK_CONFIG` | uint16_t | per boardversion | | Phase stacking topology register |
| `SYNC_CONFIG` | uint8_t | per boardversion | | Phase synchronisation mode register |
| `COMPENSATION_CONFIG` | uint8_t[5] | per boardversion | | Closed-loop compensation coefficients |

---

### Fan PID controller (`main/tasks/fan_controller_task.c`)

| Constant | Type | Value | Unit | Notes |
|---|---|---|---|---|
| `POLL_TIME_MS` | int | 100 | ms | PID sample interval |

---

### DS4432U voltage control (`main/power/DS4432U.c`)

Used on boards where voltage is regulated via the DS4432U current DAC instead of TPS546. The resistor and DAC reference values below are hardcoded for the upstream Bitaxe reference PCB feedback network; boards with different feedback resistors require different values.

| Constant | Type | Value | Unit | Notes |
|---|---|---|---|---|
| `BITAXE_IFS` | double | 0.000098921 | A | Full-scale output current; derived from (Vrfs / Rfs) × (127/16) |
| `BITAXE_RA` | double | 4750 | Ω | Upper feedback resistor (Vout to FB) |
| `BITAXE_RB` | double | 3320 | Ω | Lower feedback resistor (FB to GND) |
| `BITAXE_VNOM` | double | 1.451 | V | Nominal output voltage at DAC code = 0 |
| `BITAXE_VMAX` | double | 2.39 | V | Maximum settable output voltage |
| `BITAXE_VMIN` | double | 0.046 | V | Minimum settable output voltage |
| `TPS40305_VFB` | double | 0.6 | V | Feedback reference voltage of the regulator |

---

### Hashrate monitor task (`main/tasks/hashrate_monitor_task.c`)

| Constant | Type | Value | Unit | Notes |
|---|---|---|---|---|
| `POLL_RATE` | int | 5000 | ms | Hashrate sampling interval |
| `HASHRATE_UNIT` | uint64_t | 0x100000 | hashes | Hashrate register unit (2²⁴) |
| `HASHRATE_1M_SIZE` | int | 12 | samples | Rolling window size for 1-minute average (60 000 ms / 5000 ms) |
| `HASHRATE_10M_SIZE` | int | 10 | samples | Rolling window size for 10-minute average |
| `HASHRATE_1H_SIZE` | int | 6 | samples | Rolling window size for 1-hour average |

---

### Task polling rates

| Task | Constant | Value | Unit | Notes |
|---|---|---|---|---|
| `power_management_task.c` | `POLL_RATE` | 1800 | ms | Power management polling interval |
| `statistics_task.c` | `DEFAULT_POLL_RATE` | 5000 | ms | Default statistics sampling interval; overridable via NVS `statsFrequency` |

---

### Stratum task (`main/tasks/stratum_task.c`)

| Constant | Type | Value | Unit | Notes |
|---|---|---|---|---|
| `MAX_EXTRANONCE_2_LEN` | int | 32 | bytes | Maximum accepted extranonce2 length |
| `BUFFER_SIZE` | int | 1024 | bytes | Stratum message receive buffer |
