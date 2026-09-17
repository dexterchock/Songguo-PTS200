# Songguo PTS200

## Introduction

1. PD3.0 and QC3 fast charge protocol
2. 20V 5A 100W maximum power
3. Built-in IMU for sleep detection
4. PD protocol chip uses CH224K
5. MOSFET supports 30V 12A
6. MCU uses ESP32 S2 FH4
7. The power input uses a power-enhanced USB-C interface
8. Customized soldering tip with 4 ohm internal resistance. It can be powered by 20V with 100W.
9. 128x64 OLED screen
10. 3 buttons, the middle button is connected to GPIO0
11. MSC firmware upgrade, flash disk mode
12. With a portable tip cap

## Fixes & Improvements in This Fork

### 1. 20V Power Limit & Brownout Reset (BOR) Fix

* **Fixed 20V Duty Cycle Spikes:** In official firmware, selecting 20V 50% power mode (`POWER_LIMIT_20_2`) only applied the limit inside `Thermostat()`. Functions like `SENSORCheck()`, `SLEEPCheck()`, `setup()`, and `heatWithLimit()` improperly fell back to 100% duty cycle (`POWER_LIMIT_20`).


* **Eliminated Reboot Loop:** Because temperature sensing cycles run continuously in the main loop, the iron repeatedly blasted 100% power spikes every few milliseconds. This triggered charger Over-Current Protection (OCP) or input voltage sags, brown-outing the ESP32-S2 and forcing a restart. All control paths now properly respect `POWER_LIMIT_20_2`.



### 2. Temperature Safety & Display Improvements

* **Continuous Real Temperature Readout (Bug #19 Fix):** Removed the code that forced `ShowTemp = 0` while in lock mode. The OLED now displays actual measured tip temperature at all times, preventing burn hazards when the tip remains hot.


* **Immediate Startup Readout:** Initialized `ShowTemp` directly to measured temperature during `setup()` so live readings show immediately upon booting.


* **Faulty Tip Indicator:** Changed missing or unseated tip error display from `"000"` to `"---"` to prevent confusing an open circuit/fault with a 0°C reading.



### 3. TS-100 Tip Temperature Calibration

* **Recalibrated Linear ADC Curve:** Updated the ADC-to-temperature conversion formula inside `denoiseAnalog()` to `0.4432 * raw_adc + 29.665`. This provides accurate physical temperature tracking when using TS-100 soldering tips.



### 4. Temporary Main Screen Adjustments

* **Non-Volatile Default Presets:** Removed automated EEPROM writes when tweaking temperatures via the main screen rotary control. Temperature adjustments on the main screen now remain temporary for the current session without overwriting your saved startup defaults.



## Build Method

1. Arduino with ESP32 environment
2. Install dependent libraries: `Button2`, `U8g2`, `QC3Control`, `ESP32AnalogRead`, `PID_v1`, `SparkFun_LIS2DH12`
3. Replace the `u8g2_fonts.c` file from the `U8G2` library
4. In Arduino, select **Tools -> USB CDC On Boot -> Enable**
5. In Arduino, select **Tools -> Upload Mode -> Internal USB**
6. Click **Upload**
