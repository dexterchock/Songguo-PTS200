# Songguo PTS200 Firmware (Enhanced & Bug-Fixed Fork)

This is an improved, bug-fixed firmware fork for the **Songguo PTS200** ESP32-S2 smart soldering iron. It resolves critical flash memory wear, power regulation leaks, false tip-disconnect errors, temperature display safety hazards, and EEPROM corruption issues found in the stock firmware.

---

## Key Improvements & Fixes Over Upstream

### 1. Power Regulation & Supply Stability

* **Fixed Flash Memory Destruction (Critical):** The stock firmware executed an `EEPROM.commit()` on every tick of temperature adjustment while turning the knob or pressing buttons. This was causing extreme flash write cycles that could destroy the ESP32-S2's flash memory. Adjustments are now kept in RAM and only committed to EEPROM when exiting setup menus.
* **Centralized Power Regulation:** Replaced scattered power limit logic with a unified `getPowerLimit()` function.
* **Fixed Duty Cycle Leaks:** In the stock firmware, reduced power modes (e.g. 20V 3A / 50% duty cycle) were only applied inside `Thermostat()`. Other routines (`setup()`, `SENSORCheck()`, `SLEEPCheck()`) fell back to full power (100%), causing power supplies to brown out or trip. All heating paths now strictly follow the configured power limits.
* **Safe USB-PD Voltage Switching:** When changing voltage profiles in the menu (e.g., from 20V to 15V), the firmware now saves the setting and performs a clean reboot. This allows USB-PD chargers to renegotiate from 5V, preventing over-voltage and charger latch-ups caused by chargers that cannot step down voltages dynamically.

### 2. Temperature Safety & Sensing Fixes

* **Fixed Spurious "ERROR / New Tip" Popups:** In the original firmware, rapid heating at 20V caused the screen to flash `ERROR` and open the tip selection menu. This was caused by an undersized 2ms settle time on the OpAmp input filter. Settle time has been unified to 5ms, allowing the RC filter to fully discharge before ADC readings.
* **Live Temperature in Lock Mode (Burn Prevention):** The stock firmware forced `ShowTemp = 0` when in lock mode, hiding the actual tip temperature from the user. The display now continuously shows the live temperature so users know if the tip is still hot.
* **Clear Open-Circuit Indicator:** When a tip is missing or has a bad contact, the display now reads `---` and `ERROR` rather than `000` to clearly distinguish a sensor fault from a 0°C measurement.
* **Accurate High-Temperature Extrapolation:** Replaced discrete mapping truncations above 360°C with linear floating-point interpolation (`fmap`) for smooth, accurate readouts across higher temperatures.

### 3. Display & UI Upgrades

* **Instant OLED Startup:** Moved display initialization to the very start of `setup()`. The screen turns on within ~30ms of plugging in, eliminating the ~1-second blank screen delay present in the stock firmware.
* **400kHz Fast I2C Bus:** Upgraded the display and accelerometer I2C clock from 100kHz to 400kHz, reducing display refresh latency from ~35ms down to ~8ms for a much snappier interface and faster control loop.
* **Correct Standby Target Display:** The top-left corner displays your actual configured target temperature (`SetTemp`) during lock and off modes instead of displaying `Set: 0`.

### 4. EEPROM Stability & Auto-Repair

* **Out-of-Bounds Validation:** `read_EEPROM()` now validates stored bounds for temperatures, timers, power profiles, and calibration monotonicity ($T_{200} + 10 < T_{280} < T_{360}$). Corrupted data is automatically repaired with safe defaults on boot.
* **Symmetric Tip Name Storage:** Replaced mismatched `writeString` and `readBytes` calls with fixed-length buffer operations, preventing string truncation and memory corruption across tip profiles.

### 5. Codebase Cleanups

* **Removed Dead Dependencies:** Completely removed the unused `Button2` library and redundant code from the project.
* **No Patched Fonts Required:** Standardized font selection so the project compiles directly against the standard `U8g2` library without needing to manually replace `u8g2_fonts.c`.
* **PlatformIO Pinning:** Pinned `espressif32@~6.3.2` for consistent, reproducible builds across developer environments.

---

## Features

* **Power Protocols:** Native USB-PD (Power Delivery 3.0) via onboard CH224K sink controller.
* **Hardware:** ESP32-S2 (FH4) microcontroller, 30V 12A rated P-Channel MOSFET.
* **Display & UI:** 128x64 SH1107 OLED screen with Simple and Detailed telemetry layouts.
* **Orientation:** Selectable Left-Hand / Right-Hand screen rotation (180° flip).
* **Motion & Sleep Detection:** Integrated LIS2DH12 3-axis accelerometer for auto-sleep, auto-off, and wake-on-motion.
* **Multi-Tip Management:** Up to 8 configurable tip calibration profiles with customizable names.
* **Drag-and-Drop Upgrades:** Native Mass Storage Class (MSC) USB mode for firmware updates without external flashing tools.

---

## Building and Flashing

### Option 1: PlatformIO (Recommended)

1. Clone this repository:
```bash
git clone https://github.com/dexterchock/Songguo-PTS200.git
cd Songguo-PTS200
