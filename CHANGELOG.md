# Changelog

## v5.3.0

### Safety & Hardware Protection
* **Unblocked Heater Control During Level Exit:** Center button press to exit the 90° Spirit Level HUD returns to `loop()` immediately instead of blocking ADC temperature sampling and MOSFET regulation.
* **Auto Off Enforced in Level Mode:** Enforces the `time2off` cutoff when left idle in Spirit Level mode while continuing to skip `time2sleep` during active heat set alignment.
* **Clean Reboot on `Reset Config` and `QC3.0` Toggle:** Restoring factory defaults or toggling `QC3.0` now commits to EEPROM and reboots via `ESP.restart()` so USB PD voltage negotiation, power duty limits, and screen orientation match hardware state.
* **Calibration Return Screen Wakeup:** Exiting `Calibration` from `Off` mode keeps the OLED awake inside `Tip` setup and resets timers cleanly.
* **Fixed Zero Sensitivity Motion Lockout:** Setting `Sensitivity` to `0` (`Deactivated`) disables accelerometer wake checks cleanly instead of letting sensor baseline noise block sleep and auto off timers.
* **Safe PWM Clamp Guard for N MOSFET Builds:** Clamps output duty limits per MOSFET polarity in `Thermostat()`.

### Display, USB MSC & State Transitions
* **Safe USB MSC Event Handling:** Registers `MSC_Update.onEvent()` once, stores only volatile status codes in the ESP IDF event task, and renders `Update FW` screen updates safely from `loopTask`.
* **Smaller Binary via ASCII Unifont:** Switched OLED small text from `u8g2_font_unifont_t_chinese3` to `u8g2_font_unifont_tr` for English firmware builds while keeping identical ASCII glyphs.
* **Smoothed Level Reticle with Lock Hysteresis:** Added light EMA filtering (`alpha = 0.35`) and vertical lock hysteresis (`420` lock / `520` unlock) to remove 30 Hz boundary flicker.
* **Lock Screen Burn In Protection & Wake Refresh:** Powers off the OLED after `time2off` minutes idle in `Lock` mode (waking on any button), prevents blind temperature changes while the OLED is powered off, and refreshes timers in `wakeDisplay()`.
* **Consistent Sleep, Wake & Boost Transitions:** Short clicking in `Sleep` mode wakes cleanly instead of toggling `Boost` with a double beep, and entering `Off` or `Sleep` clears conflicting active states.
* **Fixed Tip Unplugged Setpoint Display:** Shows configured `SetTemp` instead of `Set: 0` when no tip is installed while unlocked.

### Sensing, Inputs & Release Automation
* **Clean Tip Re Insertion & Calibration Guard:** Handles tip re insertion prior to `SMOOTHIE` ADC filtering so initial readings are not blended with `1000.0`, unifies the `openCircuitThreshold` comparison, and guards `ChangeTipScreen()` during active calibration.
* **Early USB CDC Serial Startup:** Initializes `Serial` before `init_EEPROM()` so early boot error paths cannot stall USB CDC.
* **Deterministic Input & Menu Cleanup:** Converted button hold auto repeat to `millis()` timing (`400 ms` delay, `80 ms` interval), preserved existing characters and cursor column alignment in tip naming, dynamically centered tip temperature text in `MainScreen()`, and replaced byte shifted menu sizes with `ARRAY_COUNT()`.
* **Automated Tag Version Check & Dynamic Release Notes:** Verifies `config.h` matches the pushed release tag and extracts release notes directly from `CHANGELOG.md`.

## v5.2.0

* **Staged 500 ms Setup Cue:** Holding the center button sounds a single `beep()` and displays `Release: Setup` once `500 ms` is reached so users do not overshoot Setup into Spirit Level mode.
* **Live Hold Progress Bar:** Shows a `Hold: 90 Level` bar from `500 ms` to `2500 ms` while keeping sensing and heater control active until `2500 ms` double beeps into Level HUD.
* **Clean Audio Distinction:** Removed duplicate Setup entry beep (`1` beep for Setup at `500 ms`, `2` beeps for Spirit Level at `2500 ms`).

## v5.1.0

* **Screen & I2C Optimizations:** Cached `MainScreen()` telemetry to skip redundant OLED I2C transfers, capped Level HUD at ~30 Hz, set explicit `u8g2.setBusClock(400000)`, rate limited chip temperature reads to `250 ms` with `accelPresent` guarding, and added `Wire.setTimeOut(50)` recovery.
* **Lower Handle Power & Hardware Watchdog:** Lowered CPU clock to `80 MHz`, overlapped IMU/Vin work inside the `5 ms` OpAmp settle window on a `50 ms` cadence, dimmed OLED contrast in sleep, extended screen power save timing to manual `Off` mode, and enabled a `5s` `esp_task_wdt` watchdog.
* **Level Entry Non Blocking & Clean Dependencies:** Removed blocking release wait after `2500 ms` Spirit Level activation, removed unused `Button2`, and pinned `espressif32@~6.3.2`.

## v5.0.0

* **EEPROM Flash Wear & Auto Repair (`Eddddddddy` #19):** Removed EEPROM writes on temperature clicks and added boot validation/repair on stored configs (`VERSION_NUM = 500`).
* **Unified Power Limiting & USB PD Stability:** Added `getPowerLimit()`, `200 Hz` PWM, `15V` (`POWER_LIMIT_15 = 225`) bang bang heater control, and `ESP.restart()` on voltage switch.
* **Sensing & Sleep Fixes (`Eddddddddy` #8, #30):** Unified `5 ms` settle window, continuous `fmap` interpolation, `500`°C display fault cutoff, adaptive open circuit threshold, Off mode motion immunity, and `300 ms` post beep IMU blanking.
* **90° Vertical Spirit Level HUD & USB MSC Fix:** Added the live LIS2DH12 heat set alignment reticle and fixed `FirmwareMSC` tight loop registration.
