#ifndef CONFIG_H
#define CONFIG_H

// Firmware version
#define VERSION "v4.5.3" //20240130
#define VERSION_NUM 422

// Accelerometer / IMU Type (PTS200 uses LIS2DH12)
#define LIS

// Type of MOSFET
#define P_MOSFET // P_MOSFET or N_MOSFET

// Type of OLED Controller
// #define SSD1306
#define SH1107
//typedef u8g2_uint_t u8g_uint_t;
#define SCREEN_OFFSET     2

// Rotary Encoder Type
#define ROTARY_TYPE       0     // 0: 2 increments/step; 1: 4 increments/step (default)
#define BUTTON_DELAY      5

// Pins
#define SENSOR_PIN        1     // tip temperature sense
#define VIN_PIN           6     // input voltage sense
#define BUZZER_PIN        3     // buzzer
#define BUTTON_PIN        0     // switch right
#define BUTTON_P_PIN      4     // button '+'
#define BUTTON_N_PIN      2     // button '-'
#define CONTROL_PIN       5     // heater MOSFET PWM control
#define CONTROL_CHANNEL   2     // PWM channel
#define CONTROL_FREQ      200   // PWM frequency
#define CONTROL_FREQ_20V  1000  // PWM frequency for 20V
#define CONTROL_RES       8     // PWM resolution

#define PD_CFG_0          16
#define PD_CFG_1          17
#define PD_CFG_2          18

// Default Temperature Controls (°C)
#define TEMP_MIN          50    // min temp
#define TEMP_MAX          450   // max temp
#define TEMP_DEFAULT      260   // default temp
#define TEMP_SLEEP        150   // sleep temp
#define TEMP_BOOST        50    // boost step
#define TEMP_STEP         10    // rotary step
#define POWER_LIMIT_15    170   // power limit 15V
#define POWER_LIMIT_20    255   // power limit 20V
#define POWER_LIMIT_20_2  127   // secondary power limit 20V

// Default Tip Calibration Points
#define TEMP200           200   // temp at ADC = 200 
#define TEMP280           280   // temp at ADC = 280
#define TEMP360           360   // temp at ADC = 360 
#define TEMPCHP           35    // chip temp during calibration
#define CALNUM            4     // calibration point count
#define TIPMAX            8     // max tip slots
#define TIPNAMELENGTH     6     // max tip name length (including null terminator)
#define TIPNAME           "PTS  " // default tip name

// Default Timers (0 = disabled)
#define TIME2SLEEP        60    // seconds to sleep
#define TIME2OFF          5     // minutes to heater cutoff
#define TIMEOFBOOST       60    // boost duration in seconds
#define WAKEUP_THRESHOLD  10    // motion sensitivity

// Control & Settlement Delays
#define TIME2SETTLE       5000  // OpAmp output settle time (us)
#define TIME2SETTLE_20V   2000  // OpAmp output settle time at 20V (us)
#define SMOOTHIE          0.05  // OpAmp output smoothing factor (1 = no smoothing)
#define PID_ENABLE        false // enable PID control
#define BEEP_ENABLE       true  // enable/disable buzzer
#define VOLTAGE_VALUE     3     // voltage selection index
#define QC_ENABLE         false // enable/disable QC3.0
#define MAINSCREEN        1     // main screen style (0: big numbers; 1: info mode)

// EEPROM size allocation
#define EEPROM_SIZE       1024

// MOSFET Control Definitions
#if defined(P_MOSFET)           // P-Channel MOSFET
#define HEATER_ON         255
#define HEATER_OFF        0
#define HEATER_PWM        (255 - Output)
#elif defined(N_MOSFET)         // N-Channel MOSFET
#define HEATER_ON         0
#define HEATER_OFF        255
#define HEATER_PWM        Output
#else
#error Wrong MOSFET type!
#endif

// System Defaults
#define DEFAULT_LANGUAGE  0
#define DEFAULT_HAND_SIDE 1

#endif // CONFIG_H
