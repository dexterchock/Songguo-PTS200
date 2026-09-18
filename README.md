# Songguo PTS200 Firmware (Enhanced & Bug-Fixed Fork)

This is an improved, bug-fixed firmware fork for the **Songguo PTS200** ESP32-S2 powered smart soldering iron. It addresses power regulation issues, UI/safety bugs, temperature readout inaccuracies, and EEPROM state corruptions found in the original stock firmware.

---

## Key Improvements & Bug Fixes

### 1. Power Limit & Duty Cycle Leak Fixes

* **Centralized Power Regulation:** Replaced scattered power limit logic with a unified `getPowerLimit()` function.
* **Fixed Duty Cycle Leaks:** In official firmware, reduced power modes (e.g., `POWER_LIMIT_20_2`) were only applied inside `Thermostat()`. Secondary routines like `SENSORCheck()`, `SLEEPCheck()`, `setup()`, and `heatWithLimit()` fell back to full power (`POWER_LIMIT_20`). All heating and sampling paths now consistently respect your configured power caps.

### 2. Temperature Safety & Display Corrections

* **Live Temperature Readout in Lock Mode:** Removed the legacy code that forced `ShowTemp = 0` when locked. The OLED now continuously displays live temperature to prevent accidental burn hazards.
* **Immediate Startup Readout:** `ShowTemp` is initialized directly to the measured sensor temperature during boot, giving immediate live feedback rather than starting from zero.
* **Open-Circuit/Fault Indicator:** Changed the missing or unseated tip display reading from `000` to `---` to clearly distinguish a sensor error from a 0°C measurement.
* **High-Temperature Extrapolation:** Replaced discrete mapping steps beyond 360°C with linear extrapolation and floating-point interpolation (`fmap`) to maintain accuracy across high temperature ranges.

### 3. EEPROM Stability & Wear Prevention

* **Sanity Checks & Auto-Repair:** `read_EEPROM()` now validates stored bounds for temperatures, timers, power settings, tip names, and menu flags. Out-of-bounds or corrupted settings are automatically repaired with safe defaults.
* **Main Screen Adjustment Wear Reduction:** Adjusted main screen temperature quick-tuning behavior to eliminate repetitive EEPROM writes during daily operation.

### 4. Codebase & Build Cleanups

* **Header Guards & Clean Includes:** Added `include` guards (`#ifndef CONFIG_H`, etc.) across header files (`config.h`, `Languages.h`, `UtilsEEPROM.h`) to eliminate duplicate symbol issues during compilation.
* **PlatformIO Pinning:** Configured `platformio.ini` to pin `espressif32@~6.3.2` for predictable, idempotent builds across different developer environments.
* **UI & Menu Formatting:** Standardized menu navigation strings, left/right hand screen flipping, and timeout behaviors.

---

## Features

* **Power Protocols:** Support for PD3.0 and QC3.0 fast charging protocols (up to 20V 5A 100W output).
* **Hardware:** Powered by an ESP32-S2 (FH4) microcontroller, CH224K PD sink controller, and 30V 12A rated MOSFET.
* **Display & UI:** 128x64 SH1107 OLED screen with selectable Simple/Detailed main screen layouts and L/R hand orientation.
* **Motion & Sleep Detection:** Integrated LIS2DH12 3-axis accelerometer for auto-sleep, auto-off, and shake-to-wake.
* **Multi-Tip Management:** Configurable tip calibration profiles with customizable tip names.
* **Drag-and-Drop Upgrades:** Native Mass Storage Class (MSC) USB mode for firmware updates without external flashing tools.

---

## Building and Flashing

### Option 1: PlatformIO (Recommended)

1. Clone this repository:
```bash
git clone https://github.com/dexterchock/Songguo-PTS200.git
cd Songguo-PTS200

```


2. Build and upload using PlatformIO Core or the VS Code extension:
```bash
pio run -t upload

```



### Option 2: Arduino IDE

1. Install the **ESP32** board package in Arduino IDE.
2. Install the required libraries via Library Manager:
* `Button2` (v2.2.2+)
* `U8g2` (v2.34.17+)
* `QC3Control` (v1.4.1+)
* `ESP32AnalogRead` (v0.2.1+)
* `PID` (v1.2.1+)
* `SparkFun LIS2DH12 Arduino Library` (v1.0.3+)


3. Select board configuration:
* **Board:** `ESP32S2 Dev Module` (or custom PTS200 board definition)
* **USB CDC On Boot:** `Enabled`
* **Upload Mode:** `Internal USB`


4. Compile and flash `SolderingPen_ESP32S2/SolderingPen_ESP32S2.ino`.


## 📌 Credits & Acknowledgments

* Original firmware by [Eddddddddy/Songguo-PTS200](https://github.com/Eddddddddy/Songguo-PTS200?utm_source=gemini).
* Community contributors and open-source library authors (`U8g2`, `Button2`, `PID_v1`, `QC3Control`).
