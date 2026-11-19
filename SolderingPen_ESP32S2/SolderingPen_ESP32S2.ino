#include "config.h"

#include <QC3Control.h>

#include "FirmwareMSC.h"
#include "Languages.h"
#include "USB.h"
#include "UtilsEEPROM.h"

QC3Control QC(14, 13);

#include <U8g2lib.h>  
#include "PTS200_16.h"
#include <ESP32AnalogRead.h>  

#ifdef U8X8_HAVE_HW_SPI
#include <SPI.h>
#endif
#ifdef U8X8_HAVE_HW_I2C
#include <Wire.h>
#endif

#include <PID_v1.h>
#include <EEPROM.h>
#include <math.h>
#include <esp_task_wdt.h>

#include "SparkFun_LIS2DH12.h"  
SPARKFUN_LIS2DH12 accel;  

#define ACCEL_SAMPLES 32
uint16_t accels[ACCEL_SAMPLES][3];
uint8_t accelIndex = 0;
bool accelBufferReady = false;
uint32_t accelIgnoreUntil = 0;
bool accelPresent = false;

// PID parameters
double aggKp = 11, aggKi = 0.5, aggKd = 1;
double consKp = 11, consKi = 3, consKd = 5;

// EEPROM variables
uint16_t DefaultTemp = TEMP_DEFAULT;
uint16_t SleepTemp = TEMP_SLEEP;
uint8_t BoostTemp = TEMP_BOOST;
uint16_t time2sleep = TIME2SLEEP;
uint8_t time2off = TIME2OFF;
uint8_t timeOfBoost = TIMEOFBOOST;
uint8_t MainScrType = MAINSCREEN;
bool PIDenable = PID_ENABLE;
bool beepEnable = BEEP_ENABLE;
volatile uint8_t VoltageValue = VOLTAGE_VALUE;
bool QCEnable = QC_ENABLE;
uint8_t WAKEUPthreshold = WAKEUP_THRESHOLD;
bool restore_default_config = false;

// Tip defaults
uint16_t CalTemp[TIPMAX][4] = {TEMP200, TEMP280, TEMP360, TEMPCHP};
char TipName[TIPMAX][TIPNAMELENGTH] = {TIPNAME};
uint8_t CurrentTip = 0;
uint8_t NumberOfTips = 1;

// Rotary / Button variables
volatile uint8_t a0, b0, c0;
volatile int count, countMin, countMax, countStep;
volatile bool handleMoved;

// Temperature control variables
uint16_t SetTemp, ShowTemp, gap;
double Input, Output, Setpoint, RawTemp, CurrentTemp, ChipTemp;

// Voltage variables
uint16_t Vin;

#define ARRAY_COUNT(x) (sizeof(x) / sizeof((x)[0]))

// State variables
bool inLockMode = true;
bool inSleepMode = false;
bool inOffMode = false;
bool inBoostMode = false;
bool inLevelMode = false; // 90° vertical spirit level mode
bool inCalibration = false;
bool isWorky = true;
bool beepIfWorky = true;
bool TipIsPresent = true;
bool oledPowerSaved = false;
bool forceScreenRedraw = true;
bool wdtEnabled = false;

// Level HUD filter & lock hysteresis
float levelRollFilt = 0.0f;
float levelPitchFilt = 0.0f;
bool levelFiltInit = false;
bool levelLocked = false;

// USB MSC status state (written in event callback, rendered on loopTask)
enum MscDisplayState : uint8_t {
  MSC_UI_READY = 0,
  MSC_UI_START,
  MSC_UI_WRITING,
  MSC_UI_END,
  MSC_UI_ERROR,
  MSC_UI_POWER
};
volatile uint8_t mscDisplayState = MSC_UI_READY;
bool mscEventRegistered = false;

// Timing variables
uint32_t sleepmillis;
uint32_t offMillis;
uint32_t lockMillis = 0;
uint32_t boostmillis;
uint32_t buttonmillis;
uint32_t goneSeconds;
uint32_t lastSensorMs = 0;
uint32_t lastChipTempMs = 0;
uint32_t lastLevelMs = 0;
uint8_t SensorCounter = 0;

PID ctrl(&Input, &Output, &Setpoint, aggKp, aggKi, aggKd, REVERSE);

#if defined(SSD1306)
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, 22, 21);
#elif defined(SH1107)
U8G2_SH1107_64X128_F_HW_I2C u8g2(U8G2_R1, 7);
#else
#error Wrong OLED controller type!
#endif

float lastSENSORTmp = 0;

ESP32AnalogRead adc_sensor;
ESP32AnalogRead adc_vin;

uint8_t language = 0;
uint8_t hand_side = 0;

FirmwareMSC MSC_Update;
float limit = 0.0;

uint8_t getPowerLimit() {
  if (VoltageValue < 3) return POWER_LIMIT_15;
  if (VoltageValue == 3) return POWER_LIMIT_20_2;
  return POWER_LIMIT_20;
}

float fmap(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void LevelScreen();
void SENSORCheck();
void Thermostat();
void resetButtonRepeat();

void feedWatchdog() {
  if (wdtEnabled) {
    esp_task_wdt_reset();
  }
}

void wakeDisplay() {
  if (oledPowerSaved) {
    u8g2.setPowerSave(0);
    oledPowerSaved = false;
  }
  uint32_t now = millis();
  offMillis = now;
  lockMillis = now;
  u8g2.setContrast(255);
  forceScreenRedraw = true;
}

void drawHoldPrompt(uint32_t heldMs) {
  uint32_t elapsed = (heldMs > 500) ? (heldMs - 500) : 0;
  uint8_t fillWidth = constrain((int)((elapsed * 104UL) / 2000UL), 0, 104);

  u8g2.firstPage();
  do {
    u8g2.setFont(PTS200_16);
    u8g2.setFontPosTop();
    const char *title = "Release: Setup";
    uint16_t tw = u8g2.getUTF8Width(title);
    u8g2.drawUTF8((128 - tw) / 2, 4 + SCREEN_OFFSET, title);

    u8g2.setFont(u8g2_font_ncenB08_tr);
    const char *sub = "Hold: 90 Level";
    uint16_t sw = u8g2.getUTF8Width(sub);
    u8g2.drawStr((128 - sw) / 2, 26 + SCREEN_OFFSET, sub);

    u8g2.drawFrame(10, 42 + SCREEN_OFFSET, 108, 12);
    if (fillWidth > 0) {
      u8g2.drawBox(12, 44 + SCREEN_OFFSET, fillWidth, 8);
    }
  } while (u8g2.nextPage());
  forceScreenRedraw = true;
}

void updateSensorScheduled() {
  uint32_t now = millis();
  if ((uint32_t)(now - lastSensorMs) >= 50) {
    lastSensorMs = now;
    SENSORCheck();
  }
}

void setup() {
  // 1. Immediate heater cutoff for safety
  pinMode(CONTROL_PIN, OUTPUT);
  digitalWrite(CONTROL_PIN, HEATER_OFF);

  // 2. Hardware button pins & non-blocking Serial CDC
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BUTTON_P_PIN, INPUT_PULLUP);
  pinMode(BUTTON_N_PIN, INPUT_PULLUP);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);

  // 3. Load EEPROM configuration
  if (!init_EEPROM()) {
    Serial.println("EEPROM initialization failed!");
    while (true) { delay(1000); }
  }

  if (digitalRead(BUTTON_P_PIN) == LOW && digitalRead(BUTTON_N_PIN) == LOW &&
      digitalRead(BUTTON_PIN) == HIGH) {
    if (!write_default_EEPROM()) {
      Serial.println("Failed to write default EEPROM");
      while (true) { delay(1000); }
    }
  }

  if (!read_EEPROM()) {
    Serial.println("EEPROM read/repair failed!");
    while (true) { delay(1000); }
  }

  // 4. Start I2C & light up display immediately in correct orientation
  Wire.begin();
  Wire.setClock(400000);
  Wire.setTimeOut(50);
  u8g2.setBusClock(400000);
  u8g2.begin();
  u8g2.sendF("ca", 0xa8, 0x3f);
  u8g2.setContrast(255);
  u8g2.enableUTF8Print();
  u8g2.setDisplayRotation(hand_side ? U8G2_R3 : U8G2_R1);

  // 5. Clean, Minimal DEXTER Splash Screen
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_logisoso24_tr);
    u8g2.setFontPosCenter();
    uint16_t str_width = u8g2.getUTF8Width("DEXTER");
    u8g2.drawUTF8((128 - str_width) / 2, 32 + SCREEN_OFFSET, "DEXTER");
  } while (u8g2.nextPage());

  // 6. ADC Setup
  adc_sensor.attach(SENSOR_PIN);
  adc_vin.attach(VIN_PIN);

  // 7. Initiate USB-PD Handshake (Direct request to saved VoltageValue)
  pinMode(PD_CFG_0, OUTPUT);
  pinMode(PD_CFG_1, OUTPUT);
  pinMode(PD_CFG_2, OUTPUT);
  PD_Update();

  if (QCEnable) {
    QC.begin();
    delay(50);
    switch (VoltageValue) {
      case 0: QC.set9V(); break;
      case 1: QC.set12V(); break;
      case 2: QC.set12V(); break;
      case 3: QC.set20V(); break;
      case 4: QC.set20V(); break;
      default: break;
    }
  }

  // 8. Accelerometer Init
  accelPresent = accel.begin();
  if (!accelPresent) {
    Serial.println("Accelerometer not detected.");
  }

  // 9. Splash screen hold time
  delay(600);

  // 10. Measure true, settled supply voltage & initial tip presence
  Vin = getVIN();
  SetTemp = DefaultTemp;
  RawTemp = denoiseAnalog();
  calculateTemp();

  double bootThreshold = (Vin < 7000) ? 280.0 : 470.0;
  if (RawTemp > bootThreshold) {
    TipIsPresent = false;
    ShowTemp = 999;
    CurrentTemp = 999.0;
  } else {
    TipIsPresent = true;
    ShowTemp = CurrentTemp;
  }

  // 11. Controls & Rotary
  ctrl.SetOutputLimits(0, 255);
  ctrl.SetMode(AUTOMATIC);

  a0 = 0;
  b0 = 0;
  resetButtonRepeat();
  setRotary(TEMP_MIN, TEMP_MAX, TEMP_STEP, DefaultTemp);

  ChipTemp = getChipTemp();
  lastSENSORTmp = ChipTemp;
  lastChipTempMs = millis();

  // Draw main screen, then sound ready beeps
  forceScreenRedraw = true;
  MainScreen();

  sleepmillis = millis();
  offMillis = sleepmillis;
  lockMillis = sleepmillis;
  lastSensorMs = sleepmillis;
  beep();
  beep();

  // 12. Hardware task watchdog protects heater output if loop stalls
  if (esp_task_wdt_init(5, true) == ESP_OK && esp_task_wdt_add(NULL) == ESP_OK) {
    wdtEnabled = true;
  }
  Serial.println("Soldering Pen Ready");
}

void loop() {
  feedWatchdog();
  ROTARYCheck();
  SLEEPCheck();
  updateSensorScheduled();
  Thermostat();

  if (!oledPowerSaved) {
    if (inLevelMode) {
      uint32_t now = millis();
      if ((uint32_t)(now - lastLevelMs) >= 33) {
        lastLevelMs = now;
        LevelScreen(); // Renders the 90° spirit level HUD (~30 Hz)
      }
    } else {
      MainScreen(); // Skips redundant I2C flush when values are unchanged
    }
  }
  delay(1);
}

void ROTARYCheck() {
  if (oledPowerSaved) {
    if (!digitalRead(BUTTON_P_PIN) || !digitalRead(BUTTON_N_PIN)) {
      wakeDisplay();
      a0 = digitalRead(BUTTON_N_PIN);
      b0 = digitalRead(BUTTON_P_PIN);
      resetButtonRepeat();
    }
  } else {
    uint16_t newSetTemp = getRotary();
    if (newSetTemp != SetTemp) {
      SetTemp = newSetTemp;
      if (inLockMode) {
        lockMillis = millis();
      } else if (inOffMode) {
        offMillis = millis();
      } else {
        handleMoved = true;
        sleepmillis = millis();
      }
    }
  }

  uint8_t c = digitalRead(BUTTON_PIN);
  if (!c && c0) {
    delay(10);
    if (digitalRead(BUTTON_PIN) == c) {
      buttonmillis = millis();

      // If already in Level Mode, clicking center button exits immediately without blocking heater regulation
      if (inLevelMode) {
        inLevelMode = false;
        handleMoved = true;
        wakeDisplay();
        beep();
        c0 = digitalRead(BUTTON_PIN);
        return;
      }

      // Check button hold duration with staged cue at 500ms and Level Mode at 2500ms
      bool settingsCued = false;
      uint32_t lastHoldDrawMs = 0;
      while (!digitalRead(BUTTON_PIN)) {
        feedWatchdog();
        updateSensorScheduled();
        Thermostat();

        uint32_t heldMs = millis() - buttonmillis;
        if (heldMs >= 2500) {
          // Activate Level Mode and return to loop() so SENSORCheck/Thermostat keep updating live
          inLevelMode = true;
          lastLevelMs = 0;
          levelFiltInit = false;
          levelLocked = false;
          if (inSleepMode) {
            inSleepMode = false;
            beepIfWorky = true;
          }
          sleepmillis = millis();
          handleMoved = false;
          wakeDisplay();
          beep();
          beep();
          c0 = digitalRead(BUTTON_PIN);
          return;
        }

        if (heldMs >= 500) {
          if (!settingsCued) {
            settingsCued = true;
            wakeDisplay();
            beep();
            drawHoldPrompt(heldMs);
            lastHoldDrawMs = millis();
          } else if ((uint32_t)(millis() - lastHoldDrawMs) >= 40) {
            lastHoldDrawMs = millis();
            drawHoldPrompt(heldMs);
          }
        }
        delay(5);
      }

      uint32_t pressDuration = millis() - buttonmillis;

      if (settingsCued || pressDuration >= 500) {
        // HELD FOR 0.5s - 2.5s: Open Settings Menu on release
        if (!settingsCued) beep();
        SetupScreen();
      } else {
        // SHORT PRESS (< 0.5s): Handle Unlock / Wake / Boost / Off
        beep();
        if (inLockMode) {
          inLockMode = false;
          inSleepMode = false;
          inBoostMode = false;
          wakeDisplay();
          handleMoved = true;
          sleepmillis = millis();
          beepIfWorky = true;
        } else {
          buttonmillis = millis();
          while ((digitalRead(BUTTON_PIN)) && ((millis() - buttonmillis) < 200)) {
            feedWatchdog();
            updateSensorScheduled();
            Thermostat();
            delay(5);
          }
          
          if ((millis() - buttonmillis) >= 200) {  // Single click
            if (inOffMode) {
              inOffMode = false;
              inSleepMode = false;
              inBoostMode = false;
              wakeDisplay();
              handleMoved = true;
              sleepmillis = millis();
              beepIfWorky = true;
            } else if (inSleepMode) {
              inSleepMode = false;
              inBoostMode = false;
              wakeDisplay();
              handleMoved = true;
              sleepmillis = millis();
              beepIfWorky = true;
            } else {
              inBoostMode = !inBoostMode;
              if (inBoostMode) boostmillis = millis();
              handleMoved = true;
              forceScreenRedraw = true;
            }
          } else {  // Double click
            inOffMode = true;
            inSleepMode = false;
            inBoostMode = false;
            wakeDisplay();
            offMillis = millis();
          }
        }
      }
    }
  }
  c0 = c;

  if (inBoostMode && timeOfBoost) {
    goneSeconds = (millis() - boostmillis) / 1000;
    if (goneSeconds >= timeOfBoost) {
      inBoostMode = false;
      forceScreenRedraw = true;
      beep();
      beepIfWorky = true;
    }
  }
}

void SLEEPCheck() {
  if (inLevelMode) {
    if (handleMoved) {
      handleMoved = false;
      uint32_t now = millis();
      sleepmillis = now;
      offMillis = now;
      lockMillis = now;
    }
    if (!inOffMode && !inLockMode && (time2off > 0) &&
        (((millis() - sleepmillis) / 60000UL) >= time2off)) {
      inLevelMode = false;
      inOffMode = true;
      inSleepMode = false;
      inBoostMode = false;
      offMillis = millis();
      oledPowerSaved = true;
      u8g2.setPowerSave(1);
      beep();
    } else if (inLockMode && !oledPowerSaved && (time2off > 0) &&
               (((millis() - lockMillis) / 60000UL) >= time2off)) {
      inLevelMode = false;
      oledPowerSaved = true;
      u8g2.setPowerSave(1);
    } else if (inOffMode && !oledPowerSaved && (time2off > 0) &&
               (((millis() - offMillis) / 60000UL) >= time2off)) {
      inLevelMode = false;
      oledPowerSaved = true;
      u8g2.setPowerSave(1);
    }
    return;
  }

  if (inLockMode) {
    handleMoved = false;
    if (!oledPowerSaved && (time2off > 0) && (((millis() - lockMillis) / 60000UL) >= time2off)) {
      u8g2.setPowerSave(1);
      oledPowerSaved = true;
    }
    return;
  }

  if (inOffMode) {
    handleMoved = false; // Keep OLED off until explicit button wake
    if (!oledPowerSaved && (time2off > 0) && (((millis() - offMillis) / 60000UL) >= time2off)) {
      u8g2.setPowerSave(1);
      oledPowerSaved = true;
    }
    return;
  }

  if (handleMoved) {
    if (oledPowerSaved || inSleepMode) {
      wakeDisplay();
    }
    if (inSleepMode) {
      beep();
      beepIfWorky = true;
    }
    handleMoved = false;
    inSleepMode = false;
    sleepmillis = millis();
  }

  goneSeconds = (millis() - sleepmillis) / 1000;
  if ((!inSleepMode) && (time2sleep > 0) && (goneSeconds >= time2sleep)) {
    inSleepMode = true;
    inBoostMode = false;
    u8g2.setContrast(40);
    forceScreenRedraw = true;
    beep();
  } else if ((!inOffMode) && (time2off > 0) && ((goneSeconds / 60) >= time2off)) {
    inOffMode = true;
    inSleepMode = false;
    inBoostMode = false;
    offMillis = millis();
    oledPowerSaved = true;
    u8g2.setPowerSave(1);
    beep();
  }
}

void SENSORCheck() {
  // Turn heater off first so I2C IMU + Vin work overlaps the 5ms OpAmp filter discharge
  ledcWrite(CONTROL_CHANNEL, HEATER_OFF);
  uint32_t settleStart = micros();

  if (accelPresent && accel.available()) {
    int16_t rawX = accel.getRawX();
    int16_t rawY = accel.getRawY();
    int16_t rawZ = accel.getRawZ();

    // Drain sensor above, but skip buffering when disabled or during post-beep mechanical ringing
    if (WAKEUPthreshold == 0) {
      accelIndex = 0;
      accelBufferReady = false;
    } else if ((int32_t)(millis() - accelIgnoreUntil) >= 0) {
      accels[accelIndex][0] = rawX + 32768;
      accels[accelIndex][1] = rawY + 32768;
      accels[accelIndex][2] = rawZ + 32768;
      accelIndex++;

      if (accelIndex >= ACCEL_SAMPLES) {
        accelIndex = 0;
        accelBufferReady = true;
      }

      if (accelBufferReady) {
        uint64_t avg[3] = {0, 0, 0};
        for (int i = 0; i < ACCEL_SAMPLES; i++) {
          avg[0] += accels[i][0];
          avg[1] += accels[i][1];
          avg[2] += accels[i][2];
        }
        avg[0] /= ACCEL_SAMPLES;
        avg[1] /= ACCEL_SAMPLES;
        avg[2] /= ACCEL_SAMPLES;

        uint64_t var[3] = {0, 0, 0};
        for (int i = 0; i < ACCEL_SAMPLES; i++) {
          var[0] += (accels[i][0] - avg[0]) * (accels[i][0] - avg[0]);
          var[1] += (accels[i][1] - avg[1]) * (accels[i][1] - avg[1]);
          var[2] += (accels[i][2] - avg[2]) * (accels[i][2] - avg[2]);
        }
        var[0] /= ACCEL_SAMPLES;
        var[1] /= ACCEL_SAMPLES;
        var[2] /= ACCEL_SAMPLES;

        uint64_t vThresh = (uint64_t)WAKEUPthreshold * 10000ULL;
        if (var[0] > vThresh || var[1] > vThresh || var[2] > vThresh) {
          handleMoved = true;
          accelIndex = 0;
          accelBufferReady = false;
        }
      }
    }
  }

  if (SensorCounter++ > 10) {
    Vin = getVIN();
    SensorCounter = 0;
  }

  uint32_t elapsed = micros() - settleStart;
  if (elapsed < TIME2SETTLE) {
    delayMicroseconds(TIME2SETTLE - elapsed);
  }

  double temp = denoiseAnalog();

  // Adaptive threshold: 280°C on 5V weak USB, 470°C on fast chargers
  double openCircuitThreshold = (Vin < 7000) ? 280.0 : 470.0;

  if (temp >= openCircuitThreshold) {
    // TIP IS UNPLUGGED: Instant bypass of smoothing filter
    TipIsPresent = false;
    RawTemp = temp;
    CurrentTemp = 999.0;
    ShowTemp = 999;
    ledcWrite(CONTROL_CHANNEL, HEATER_OFF);
  } else if (!TipIsPresent) {
    // TIP RE-INSERTED: Reset filter before smoothing and optionally prompt tip selection
    ledcWrite(CONTROL_CHANNEL, HEATER_OFF);
    TipIsPresent = true;
    RawTemp = temp;
    calculateTemp();
    ShowTemp = CurrentTemp;
    if (!inCalibration) {
      beep();
      wakeDisplay();
      ChangeTipScreen();
      if (!update_EEPROM()) Serial.println("EEPROM update failed on tip change");
      handleMoved = true;
      forceScreenRedraw = true;
      c0 = LOW;
      setRotary(TEMP_MIN, TEMP_MAX, TEMP_STEP, SetTemp);
    }
  } else {
    // TIP IS PLUGGED IN: Smooth filter keeps display calm
    RawTemp += (temp - RawTemp) * SMOOTHIE;
    calculateTemp();

    if ((ShowTemp != Setpoint) || (abs(ShowTemp - CurrentTemp) > 5))
      ShowTemp = CurrentTemp;
    if (abs(ShowTemp - Setpoint) <= 1) ShowTemp = Setpoint;
  }

  double targetTemp = (!TipIsPresent || inOffMode || inLockMode)
                          ? SetTemp
                          : (inSleepMode ? SleepTemp
                                         : (inBoostMode ? constrain(SetTemp + BoostTemp, 0, TEMP_MAX) : SetTemp));
  gap = abs(targetTemp - CurrentTemp);
  if (gap < 5) {
    if (!isWorky && beepIfWorky) beep();
    isWorky = true;
    beepIfWorky = false;
  } else {
    isWorky = false;
  }
}

void calculateTemp() {
  if (RawTemp < 200) {
    CurrentTemp = fmap(RawTemp, 0, 200, 15, CalTemp[CurrentTip][0]);
  } else if (RawTemp < 280) {
    CurrentTemp = fmap(RawTemp, 200, 280, CalTemp[CurrentTip][0], CalTemp[CurrentTip][1]);
  } else {
    CurrentTemp = fmap(RawTemp, 280, 360, CalTemp[CurrentTip][1], CalTemp[CurrentTip][2]);
  }
}

void Thermostat() {
  if (!TipIsPresent || inOffMode || inLockMode) {
    Setpoint = 0;
    Output = 0;
    ledcWrite(CONTROL_CHANNEL, HEATER_OFF);
    return;
  }

  if (inSleepMode)
    Setpoint = SleepTemp;
  else if (inBoostMode)
    Setpoint = constrain(SetTemp + BoostTemp, 0, 450);
  else
    Setpoint = SetTemp;

  gap = abs(Setpoint - CurrentTemp);
  if (PIDenable) {
    Input = CurrentTemp;
    if (gap < 30)
      ctrl.SetTunings(consKp, consKi, consKd);
    else
      ctrl.SetTunings(aggKp, aggKi, aggKd);
      
    limit = getPowerLimit();
    ctrl.SetOutputLimits(255 - limit, 255);
    ctrl.Compute();
  } else {
    if ((CurrentTemp + 0.5) < Setpoint)
      Output = 0;
    else
      Output = 255;
  }
  
  limit = getPowerLimit();
#if defined(P_MOSFET)
  ledcWrite(CONTROL_CHANNEL, constrain((int)(HEATER_PWM), 0, (int)limit));
#elif defined(N_MOSFET)
  ledcWrite(CONTROL_CHANNEL, constrain((int)(HEATER_PWM), 255 - (int)limit, 255));
#endif
}

// ----------------------------------------------------------------------------
// 90° VERTICAL SPIRIT LEVEL HUD (CLEAN EDGE-TO-EDGE RETICLE)
// ----------------------------------------------------------------------------
void LevelScreen() {
  int16_t rawX = accelPresent ? accel.getRawX() : 0;
  int16_t rawY = accelPresent ? accel.getRawY() : 0;
  int16_t rawZ = accelPresent ? accel.getRawZ() : 0;

  // Determine shaft orientation dynamically:
  // Whichever axis has > 8000 counts is pointing down with gravity
  int16_t rawRoll = 0;
  int16_t rawPitch = 0;
  int16_t shaftVal = 0;

  if (abs(rawX) > abs(rawY)) {
    shaftVal = rawX;
    rawRoll = rawY;
    rawPitch = rawZ;
  } else {
    shaftVal = rawY;
    rawRoll = rawX;
    rawPitch = rawZ;
  }

  if (hand_side) {
    rawRoll = -rawRoll;
    rawPitch = -rawPitch;
  }

  if (!levelFiltInit) {
    levelRollFilt = (float)rawRoll;
    levelPitchFilt = (float)rawPitch;
    levelFiltInit = true;
  } else {
    levelRollFilt += 0.35f * ((float)rawRoll - levelRollFilt);
    levelPitchFilt += 0.35f * ((float)rawPitch - levelPitchFilt);
  }

  int16_t tiltRoll = (int16_t)roundf(levelRollFilt);
  int16_t tiltPitch = (int16_t)roundf(levelPitchFilt);

  // Y-Axis Rotation: Left/Right Roll tilts the center line angle
  int8_t rollAngle = constrain(tiltRoll / 180, -9, 9);

  // Perpendicular hysteresis: lock < 420, unlock > 520 (eliminates 30 Hz boundary jitter)
  if (levelLocked) {
    if ((abs(shaftVal) < 8200) || (abs(tiltRoll) > 520) || (abs(tiltPitch) > 520)) {
      levelLocked = false;
    }
  } else {
    if ((abs(shaftVal) > 9000) && (abs(tiltRoll) < 420) && (abs(tiltPitch) < 420)) {
      levelLocked = true;
    }
  }
  bool isVertical = levelLocked;

  u8g2.firstPage();
  do {
    // 1. TOP ROW: Detailed Top UI Layout (Preserved from MainScreen)
    u8g2.setFont(PTS200_16);
    u8g2.setFontPosTop();
    u8g2.drawUTF8(0, 0 + SCREEN_OFFSET, txt_set_temp[language]);
    u8g2.setCursor(40, 0 + SCREEN_OFFSET);
    u8g2.setFont(u8g2_font_unifont_tr);

    uint16_t dispSet = (!TipIsPresent || inOffMode || inLockMode) ? SetTemp : (inSleepMode ? SleepTemp : (uint16_t)Setpoint);
    u8g2.print(dispSet);

    u8g2.setFont(PTS200_16);
    const char *status_str = txt_hold[language];
    if (!TipIsPresent || ShowTemp > 500) status_str = txt_error[language];
    else if (inOffMode || inLockMode) status_str = txt_off[language];
    else if (inSleepMode) status_str = txt_sleep[language];
    else if (inBoostMode) status_str = txt_boost[language];
    else if (isWorky) status_str = txt_worky[language];
    else if (Output < 180) status_str = txt_on[language];

    uint16_t str_width = u8g2.getUTF8Width(status_str);
    u8g2.setCursor(128 - str_width, 0 + SCREEN_OFFSET);
    u8g2.print(status_str);

    // 2. CENTER: Clean Edge-to-Edge Level Lines (Base Y = 32, no "T" notches)
    const int8_t baseY = 32 + SCREEN_OFFSET;

    // Left Line: Extends from very edge (X: 0 to 42)
    u8g2.drawHLine(0, baseY, 43);
    u8g2.drawHLine(0, baseY + 1, 43);

    // Right Line: Extends to very edge (X: 86 to 127)
    u8g2.drawHLine(86, baseY, 42);
    u8g2.drawHLine(86, baseY + 1, 42);

    // Center Dynamic Line: Centered horizontally between X: 46 and X: 82
    if (isVertical) {
      // PERFECT 90° VERTICAL: Connects from edge to edge into one solid bar!
      u8g2.drawHLine(0, baseY, 128);
      u8g2.drawHLine(0, baseY + 1, 128);
    } else {
      // Roll tilts the line left/right
      int8_t yL = baseY - rollAngle;
      int8_t yR = baseY + rollAngle;

      // Pitch modulates thickness (X-Axis depth):
      // Further -> thinner (1px or dotted)
      // Nearer -> thicker (3px, 4px, 5px)
      if (tiltPitch < -1100) {
        // Tilted far away: Dotted line
        for (int8_t x = 46; x <= 82; x += 3) {
          int8_t y = yL + (int16_t)(yR - yL) * (x - 46) / 36;
          u8g2.drawPixel(x, y);
        }
      } else if (tiltPitch < -450) {
        // Tilted slightly away: 1px thin line
        u8g2.drawLine(46, yL, 82, yR);
      } else if (tiltPitch <= 450) {
        // Pitch centered: 2px line (exact match to left and right reference lines)
        u8g2.drawLine(46, yL, 82, yR);
        u8g2.drawLine(46, yL + 1, 82, yR + 1);
      } else if (tiltPitch <= 1200) {
        // Tilted nearer: 3px thick
        u8g2.drawLine(46, yL - 1, 82, yR - 1);
        u8g2.drawLine(46, yL,     82, yR);
        u8g2.drawLine(46, yL + 1, 82, yR + 1);
      } else {
        // Tilted much nearer: 5px heavy bar
        for (int8_t t = -2; t <= 2; t++) {
          u8g2.drawLine(46, yL + t, 82, yR + t);
        }
      }
    }

    // 3. BOTTOM ROW: Live Current Tip Temperature on the RIGHT (clean & unblocked)
    u8g2.setFont(u8g2_font_unifont_tr);
    u8g2.setFontPosTop();
    char tempBuf[12];
    if (!TipIsPresent || ShowTemp > 500) snprintf(tempBuf, sizeof(tempBuf), "---C");
    else snprintf(tempBuf, sizeof(tempBuf), "%03dC", ShowTemp);

    uint16_t tw = u8g2.getUTF8Width(tempBuf);
    u8g2.setCursor(128 - tw, 48 + SCREEN_OFFSET);
    u8g2.print(tempBuf);

  } while (u8g2.nextPage());
}

void beep() {
  if (beepEnable) {
    for (uint8_t i = 0; i < 255; i++) {
      digitalWrite(BUZZER_PIN, HIGH);
      delayMicroseconds(125);
      digitalWrite(BUZZER_PIN, LOW);
      delayMicroseconds(125);
    }
    accelIgnoreUntil = millis() + 300;
    accelIndex = 0;
    accelBufferReady = false;
  }
}

void setRotary(int rmin, int rmax, int rstep, int rvalue) {
  countMin = rmin << ROTARY_TYPE;
  countMax = rmax << ROTARY_TYPE;
  countStep = rstep;
  count = rvalue << ROTARY_TYPE;
}

int getRotary() {
  Button_loop();
  return (count >> ROTARY_TYPE);
}

bool getEEPROM() { return read_EEPROM(); }
bool updateEEPROM() { return update_EEPROM(); }

void MainScreen() {
  uint32_t now = millis();
  if (MainScrType && ((uint32_t)(now - lastChipTempMs) >= 250)) {
    lastChipTempMs = now;
    lastSENSORTmp += 0.1f * ((float)getChipTemp() - lastSENSORTmp);
  }

  uint16_t dispSet = (!TipIsPresent || inOffMode || inLockMode) ? SetTemp : (inSleepMode ? SleepTemp : (uint16_t)Setpoint);
  uint16_t dispTemp = (!TipIsPresent || ShowTemp > 500) ? 999 : ShowTemp;

  const char *status_str = txt_hold[language];
  if (!TipIsPresent || ShowTemp > 500) status_str = txt_error[language];
  else if (inOffMode || inLockMode) status_str = txt_off[language];
  else if (inSleepMode) status_str = txt_sleep[language];
  else if (inBoostMode) status_str = txt_boost[language];
  else if (isWorky) status_str = txt_worky[language];
  else if (Output < 180) status_str = txt_on[language];

  float fVin = (float)Vin / 1000.0f;
  int16_t chipTenths = MainScrType ? (int16_t)roundf(lastSENSORTmp * 10.0f) : 0;
  int16_t vinTenths = MainScrType ? (int16_t)roundf(fVin * 10.0f) : 0;

  static bool cacheValid = false;
  static uint16_t cachedSet = 0xFFFF;
  static uint16_t cachedTemp = 0xFFFF;
  static const char *cachedStatus = nullptr;
  static uint8_t cachedScrType = 0xFF;
  static int16_t cachedChipTenths = -32768;
  static int16_t cachedVinTenths = -32768;

  if (!forceScreenRedraw && cacheValid &&
      cachedSet == dispSet &&
      cachedTemp == dispTemp &&
      cachedStatus == status_str &&
      cachedScrType == MainScrType &&
      cachedChipTenths == chipTenths &&
      cachedVinTenths == vinTenths) {
    return;
  }

  cacheValid = true;
  forceScreenRedraw = false;
  cachedSet = dispSet;
  cachedTemp = dispTemp;
  cachedStatus = status_str;
  cachedScrType = MainScrType;
  cachedChipTenths = chipTenths;
  cachedVinTenths = vinTenths;

  char tempBuf[8];
  if (dispTemp == 999) snprintf(tempBuf, sizeof(tempBuf), "---");
  else snprintf(tempBuf, sizeof(tempBuf), "%03d", dispTemp);

  u8g2.firstPage();
  do {
    u8g2.setFont(PTS200_16);
    u8g2.setFontPosTop();
    u8g2.drawUTF8(0, 0 + SCREEN_OFFSET, txt_set_temp[language]);
    u8g2.setCursor(40, 0 + SCREEN_OFFSET);
    u8g2.setFont(u8g2_font_unifont_tr);
    u8g2.print(dispSet);

    u8g2.setFont(PTS200_16);
    uint16_t str_width = u8g2.getUTF8Width(status_str);
    u8g2.setCursor(128 - str_width, 0 + SCREEN_OFFSET);
    u8g2.print(status_str);

    u8g2.setFont(u8g2_font_unifont_tr);
    if (MainScrType) {
      u8g2.setCursor(0, 50);
      u8g2.print(lastSENSORTmp, 1);
      u8g2.print(F("C"));
      u8g2.setCursor(83, 50);
      u8g2.print(fVin, 1);
      u8g2.print(F("V"));

      u8g2.setFont(u8g2_font_freedoomr25_tn);
      u8g2.setFontPosTop();
      int16_t tx = (128 - (int16_t)u8g2.getStrWidth(tempBuf)) / 2;
      u8g2.setCursor(tx, 18);
      u8g2.print(tempBuf);
    } else {
      u8g2.setFont(u8g2_font_fub42_tn);
      u8g2.setFontPosTop();
      int16_t tx = (128 - (int16_t)u8g2.getStrWidth(tempBuf)) / 2;
      u8g2.setCursor(tx, 20);
      u8g2.print(tempBuf);
    }
  } while (u8g2.nextPage());
}

void SetupScreen() {
  ledcWrite(CONTROL_CHANNEL, HEATER_OFF);
  wakeDisplay();
  uint16_t SaveSetTemp = SetTemp;
  uint8_t selection = 0;
  bool repeat = true;
  bool eepromOperationFailed = false;

  while (repeat) {
    selection = MenuScreen(SetupItems, ARRAY_COUNT(SetupItems), selection);
    switch (selection) {
      case 0: TipScreen(); break;
      case 1: TempScreen(); break;
      case 2: TimerScreen(); break;
      case 3: MainScrType = MenuScreen(MainScreenItems, ARRAY_COUNT(MainScreenItems), MainScrType); break;
      case 4: InfoScreen(); break;
      case 5: {
        uint8_t oldVolt = VoltageValue;
        VoltageValue = MenuScreen(VoltageItems, ARRAY_COUNT(VoltageItems), VoltageValue);
        if (oldVolt != VoltageValue) {
          update_EEPROM();
          u8g2.clearBuffer();
          u8g2.setFont(PTS200_16);
          u8g2.setFontPosTop();
          u8g2.drawUTF8(0, 24 + SCREEN_OFFSET, "Switching...");
          u8g2.sendBuffer();
          feedWatchdog();
          delay(400);
          ESP.restart(); // Clean handshake reset from 5V
        }
      } break;
      case 6: {
        bool oldQC = QCEnable;
        QCEnable = MenuScreen(QCItems, ARRAY_COUNT(QCItems), QCEnable);
        if (oldQC != QCEnable) {
          update_EEPROM();
          u8g2.clearBuffer();
          u8g2.setFont(PTS200_16);
          u8g2.setFontPosTop();
          u8g2.drawUTF8(0, 24 + SCREEN_OFFSET, "Switching...");
          u8g2.sendBuffer();
          feedWatchdog();
          delay(400);
          ESP.restart(); // Re-initialize USB QC state on boot
        }
      } break;
      case 7: beepEnable = MenuScreen(BuzzerItems, ARRAY_COUNT(BuzzerItems), beepEnable); break;
      case 8: 
        restore_default_config = MenuScreen(DefaultItems, ARRAY_COUNT(DefaultItems), restore_default_config);
        if (restore_default_config) {
          restore_default_config = false;
          if (!write_default_EEPROM()) {
            Serial.println("Failed to write default EEPROM");
            eepromOperationFailed = true;
            repeat = false;
            break;
          }
          u8g2.clearBuffer();
          u8g2.setFont(PTS200_16);
          u8g2.setFontPosTop();
          u8g2.drawUTF8(0, 24 + SCREEN_OFFSET, "Resetting...");
          u8g2.sendBuffer();
          feedWatchdog();
          delay(400);
          ESP.restart(); // Cleanly re-negotiate USB-PD, OLED orientation, and defaults
        }
        break;
      case 9: {
        bool lastbutton = (!digitalRead(BUTTON_PIN));
        mscDisplayState = MSC_UI_READY;
        uint8_t lastDrawnState = MSC_UI_READY;
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_ncenB08_tr);
        u8g2.drawStr(0, 10, "MSC Update");
        u8g2.sendBuffer();
        feedWatchdog();
        delay(500);
        if (!mscEventRegistered) {
          MSC_Update.onEvent(usbEventCallback);
          mscEventRegistered = true;
        }
        MSC_Update.begin();
        do {
          feedWatchdog();
          uint8_t curState = mscDisplayState;
          if (curState != lastDrawnState) {
            lastDrawnState = curState;
            const char *msg = "MSC Update";
            switch (curState) {
              case MSC_UI_START:   msg = "MSC Start"; break;
              case MSC_UI_WRITING: msg = "MSC Updating"; break;
              case MSC_UI_END:     msg = "MSC End"; break;
              case MSC_UI_ERROR:   msg = "MSC ERROR!"; break;
              case MSC_UI_POWER:   msg = "MSC Power"; break;
              default:             msg = "MSC Update"; break;
            }
            u8g2.clearBuffer();
            u8g2.setFont(u8g2_font_ncenB08_tr);
            u8g2.drawStr(0, 10, msg);
            u8g2.sendBuffer();
          }
          delay(10);
          if (lastbutton && digitalRead(BUTTON_PIN)) {
            delay(10);
            lastbutton = false;
          }
        } while (digitalRead(BUTTON_PIN) || lastbutton);
        MSC_Update.end();
      } break;
      case 10: 
        hand_side = (hand_side == 0) ? 1 : 0;
        u8g2.setDisplayRotation(hand_side ? U8G2_R3 : U8G2_R1);
        break;
      case 11:
        repeat = false;
        break;
      default: repeat = false; break;
    }
  }

  if (!eepromOperationFailed) {
    if (!update_EEPROM()) Serial.println("EEPROM update failed at setup exit");
  }

  handleMoved = true;
  wakeDisplay();
  SetTemp = SaveSetTemp;
  setRotary(TEMP_MIN, TEMP_MAX, TEMP_STEP, SetTemp);
}

void TipScreen() {
  uint8_t selection = 0;
  bool repeat = true;
  while (repeat) {
    selection = MenuScreen(TipItems, ARRAY_COUNT(TipItems), selection);
    switch (selection) {
      case 0: ChangeTipScreen(); break;
      case 1: CalibrationScreen(); break;
      case 2: InputNameScreen(); break;
      case 3: DeleteTipScreen(); break;
      case 4: AddTipScreen(); break;
      case 5: repeat = false; break;
      default: repeat = false; break;
    }
  }
}

void TempScreen() {
  uint8_t selection = 0;
  bool repeat = true;
  while (repeat) {
    selection = MenuScreen(TempItems, ARRAY_COUNT(TempItems), selection);
    switch (selection) {
      case 0:
        setRotary(TEMP_MIN, TEMP_MAX, TEMP_STEP, DefaultTemp);
        DefaultTemp = InputScreen(DefaultTempItems);
        break;
      case 1:
        setRotary(50, TEMP_MAX, TEMP_STEP, SleepTemp);
        SleepTemp = InputScreen(SleepTempItems);
        break;
      case 2:
        setRotary(10, 100, TEMP_STEP, BoostTemp);
        BoostTemp = InputScreen(BoostTempItems);
        break;
      case 3: repeat = false; break;
      default: repeat = false; break;
    }
  }
}

void TimerScreen() {
  uint8_t selection = 0;
  bool repeat = true;
  while (repeat) {
    selection = MenuScreen(TimerItems, ARRAY_COUNT(TimerItems), selection);
    switch (selection) {
      case 0:
        setRotary(0, 600, 10, time2sleep);
        time2sleep = InputScreen(SleepTimerItems);
        break;
      case 1:
        setRotary(0, 60, 1, time2off);
        time2off = InputScreen(OffTimerItems);
        break;
      case 2:
        setRotary(0, 180, 10, timeOfBoost);
        timeOfBoost = InputScreen(BoostTimerItems);
        break;
      case 3:
        setRotary(0, 50, 5, WAKEUPthreshold);
        WAKEUPthreshold = InputScreen(WAKEUPthresholdItems);
        break;
      case 4: repeat = false; break;
      default: repeat = false; break;
    }
  }
}

uint8_t MenuScreen(const char *Items[][language_types], uint8_t numberOfItems, uint8_t selected) {
  bool isTipScreen = ((strcmp(Items[0][language], "Tip:") == 0) ||
                      (strcmp(Items[0][language], "Tip") == 0));
  uint8_t lastselected = selected;
  int8_t arrow = 0;
  if (selected) arrow = 1;

  setRotary(0, numberOfItems - 2, 1, selected);

  bool lastbutton = (!digitalRead(BUTTON_PIN));
  do {
    feedWatchdog();
    selected = getRotary();
    arrow = constrain(arrow + selected - lastselected, 0, 2);
    lastselected = selected;
    u8g2.firstPage();
    do {
      u8g2.setFont(PTS200_16);
      u8g2.setFontPosTop();
      u8g2.drawUTF8(0, 0 + SCREEN_OFFSET, Items[0][language]);
      if (isTipScreen) u8g2.drawUTF8(54, 0 + SCREEN_OFFSET, TipName[CurrentTip]);
      u8g2.drawUTF8(0, 16 * (arrow + 1) + SCREEN_OFFSET, ">");
      for (uint8_t i = 0; i < 3; i++) {
        uint8_t drawnumber = selected + i + 1 - arrow;
        if (drawnumber < numberOfItems)
          u8g2.drawUTF8(12, 16 * (i + 1) + SCREEN_OFFSET, Items[selected + i + 1 - arrow][language]);
      }
    } while (u8g2.nextPage());
    if (lastbutton && digitalRead(BUTTON_PIN)) {
      delay(10);
      lastbutton = false;
    }
  } while (digitalRead(BUTTON_PIN) || lastbutton);

  beep();
  return selected;
}

void MessageScreen(const char *Items[][language_types], uint8_t numberOfItems) {
  bool lastbutton = (!digitalRead(BUTTON_PIN));
  u8g2.firstPage();
  do {
    u8g2.setFont(PTS200_16);
    u8g2.setFontPosTop();
    for (uint8_t i = 0; i < numberOfItems; i++)
      u8g2.drawUTF8(0, i * 16, Items[i][language]);
  } while (u8g2.nextPage());
  do {
    feedWatchdog();
    if (lastbutton && digitalRead(BUTTON_PIN)) {
      delay(10);
      lastbutton = false;
    }
  } while (digitalRead(BUTTON_PIN) || lastbutton);
  beep();
}

uint16_t InputScreen(const char *Items[][language_types]) {
  uint16_t value;
  bool lastbutton = (!digitalRead(BUTTON_PIN));
  do {
    feedWatchdog();
    value = getRotary();
    u8g2.firstPage();
    do {
      u8g2.setFont(PTS200_16);
      u8g2.setFontPosTop();
      u8g2.drawUTF8(0, 0 + SCREEN_OFFSET, Items[0][language]);
      u8g2.setCursor(0, 32);
      u8g2.print(">");
      u8g2.setCursor(10, 32);
      if (value == 0) u8g2.print(txt_Deactivated[language]);
      else {
        u8g2.print(value);
        u8g2.print(" ");
        u8g2.print(Items[1][language]);
      }
    } while (u8g2.nextPage());
    if (lastbutton && digitalRead(BUTTON_PIN)) {
      delay(10);
      lastbutton = false;
    }
  } while (digitalRead(BUTTON_PIN) || lastbutton);

  beep();
  return value;
}

void InfoScreen() {
  bool lastbutton = (!digitalRead(BUTTON_PIN));
  do {
    feedWatchdog();
    Vin = getVIN();
    float fVin = (float)Vin / 1000.0f;
    float fTmp = getChipTemp();
    u8g2.firstPage();
    do {
      u8g2.setFont(PTS200_16);
      u8g2.setFontPosTop();
      u8g2.setCursor(0, 0 + SCREEN_OFFSET);
      u8g2.print(txt_temp[language]);
      u8g2.print(fTmp, 1);
      u8g2.print(F(" C"));
      u8g2.setCursor(0, 16 + SCREEN_OFFSET);
      u8g2.print(txt_voltage[language]);
      u8g2.print(fVin, 1);
      u8g2.print(F(" V"));
      u8g2.setCursor(0, 16 * 2 + SCREEN_OFFSET);
      u8g2.print(txt_Version[language]);
      u8g2.print(VERSION);
    } while (u8g2.nextPage());
    if (lastbutton && digitalRead(BUTTON_PIN)) {
      delay(10);
      lastbutton = false;
    }
  } while (digitalRead(BUTTON_PIN) || lastbutton);

  beep();
}

void ChangeTipScreen() {
  uint8_t selected = CurrentTip;
  uint8_t lastselected = selected;
  int8_t arrow = 0;
  if (selected) arrow = 1;
  setRotary(0, NumberOfTips - 1, 1, selected);
  bool lastbutton = (!digitalRead(BUTTON_PIN));

  do {
    feedWatchdog();
    selected = getRotary();
    arrow = constrain(arrow + selected - lastselected, 0, 2);
    lastselected = selected;
    u8g2.firstPage();
    do {
      u8g2.setFont(PTS200_16);
      u8g2.setFontPosTop();
      u8g2.drawUTF8(0, 0 + SCREEN_OFFSET, txt_select_tip[language]);
      u8g2.drawUTF8(0, 16 * (arrow + 1) + SCREEN_OFFSET, ">");
      for (uint8_t i = 0; i < 3; i++) {
        uint8_t drawnumber = selected + i - arrow;
        if (drawnumber < NumberOfTips)
          u8g2.drawUTF8(12, 16 * (i + 1) + SCREEN_OFFSET, TipName[selected + i - arrow]);
      }
    } while (u8g2.nextPage());
    if (lastbutton && digitalRead(BUTTON_PIN)) {
      delay(10);
      lastbutton = false;
    }
  } while (digitalRead(BUTTON_PIN) || lastbutton);

  beep();
  CurrentTip = selected;
}

void CalibrationScreen() {
  bool savedLockMode = inLockMode;
  bool savedSleepMode = inSleepMode;
  bool savedOffMode = inOffMode;
  bool savedBoostMode = inBoostMode;
  bool savedHandleMoved = handleMoved;
  bool savedBeepIfWorky = beepIfWorky;
  bool savedTipIsPresent = TipIsPresent;
  uint32_t savedSleepMillis = sleepmillis;
  uint32_t savedBoostMillis = boostmillis;

  inCalibration = true;
  inLockMode = false;
  inSleepMode = false;
  inOffMode = false;
  inBoostMode = false;
  handleMoved = true;
  sleepmillis = millis();

  uint16_t CalTempNew[4];
  uint16_t tempSetTemp = SetTemp;
  for (uint8_t CalStep = 0; CalStep < 3; CalStep++) {
    SetTemp = CalTemp[CurrentTip][CalStep];
    setRotary(100, 500, 1, SetTemp);
    beepIfWorky = true;
    bool lastbutton = (!digitalRead(BUTTON_PIN));

    do {
      feedWatchdog();
      SENSORCheck();
      Thermostat();

      if (CurrentTemp > 500.0) {
        ledcWrite(CONTROL_CHANNEL, HEATER_OFF);
        
        SetTemp = tempSetTemp;
        inLockMode = savedLockMode;
        inSleepMode = savedSleepMode;
        inOffMode = savedOffMode;
        inBoostMode = savedBoostMode;
        handleMoved = savedHandleMoved;
        beepIfWorky = savedBeepIfWorky;
        TipIsPresent = savedTipIsPresent;
        sleepmillis = savedSleepMillis;
        boostmillis = savedBoostMillis;
        inCalibration = false;
        wakeDisplay();
        return;
      }

      u8g2.firstPage();
      do {
        u8g2.setFont(PTS200_16);
        u8g2.setFontPosTop();
        u8g2.drawUTF8(0, 0 + SCREEN_OFFSET, txt_calibrate[language]);
        u8g2.setCursor(0, 16 + SCREEN_OFFSET);
        u8g2.print(txt_step[language]);
        u8g2.print(CalStep + 1);
        u8g2.print(" of 3");
        if (isWorky) {
          u8g2.setCursor(0, 32 + SCREEN_OFFSET);
          u8g2.print(txt_set_measured[language]);
          u8g2.setCursor(0, 48 + SCREEN_OFFSET);
          u8g2.print(txt_s_temp[language]);
          u8g2.print(getRotary());
        } else {
          u8g2.setCursor(0, 32 + SCREEN_OFFSET);
          u8g2.print(txt_temp_2[language]);
          u8g2.print(uint16_t(RawTemp));
          u8g2.setCursor(0, 48 + SCREEN_OFFSET);
          u8g2.print(txt_wait_pls[language]);
        }
      } while (u8g2.nextPage());
      if (lastbutton && digitalRead(BUTTON_PIN)) {
        delay(10);
        lastbutton = false;
      }
    } while (digitalRead(BUTTON_PIN) || lastbutton);

    CalTempNew[CalStep] = getRotary();
    beep();
    delay(10);
  }

  ledcWrite(CONTROL_CHANNEL, HEATER_OFF);
  delayMicroseconds(TIME2SETTLE);
  
  CalTempNew[3] = getChipTemp();
  if ((CalTempNew[0] + 10 < CalTempNew[1]) &&
      (CalTempNew[1] + 10 < CalTempNew[2])) {
    if (MenuScreen(StoreItems, ARRAY_COUNT(StoreItems), 0)) {
      for (uint8_t i = 0; i < 4; i++) CalTemp[CurrentTip][i] = CalTempNew[i];
    }
  }

  SetTemp = tempSetTemp;

  inLockMode = savedLockMode;
  inSleepMode = savedSleepMode;
  inOffMode = savedOffMode;
  inBoostMode = savedBoostMode;
  handleMoved = savedHandleMoved;
  beepIfWorky = savedBeepIfWorky;
  TipIsPresent = savedTipIsPresent;
  sleepmillis = savedSleepMillis;
  boostmillis = savedBoostMillis;
  inCalibration = false;
  wakeDisplay();

  if (!update_EEPROM()) Serial.println("EEPROM update failed on calibration finish");
}

void InputNameScreen() {
  uint8_t value;
  for (uint8_t digit = 0; digit < (TIPNAMELENGTH - 1); digit++) {
    bool lastbutton = (!digitalRead(BUTTON_PIN));
    uint8_t initChar = (uint8_t)TipName[CurrentTip][digit];
    if (initChar < 32 || initChar > 95) initChar = 65; // 'A'
    setRotary(31, 96, 1, initChar);
    do {
      feedWatchdog();
      value = getRotary();
      if (value == 31) { value = 95; setRotary(31, 96, 1, 95); }
      if (value == 96) { value = 32; setRotary(31, 96, 1, 32); }
      u8g2.firstPage();
      do {
        u8g2.setFont(PTS200_16);
        u8g2.setFontPosTop();
        u8g2.drawUTF8(0, 0 + SCREEN_OFFSET, txt_enter_tip_name[language]);
        u8g2.setCursor(12 * digit, 48 + SCREEN_OFFSET);
        u8g2.print(char(94));
        for (uint8_t i = 0; i < digit; i++) {
          u8g2.setCursor(12 * i, 32 + SCREEN_OFFSET);
          u8g2.print(TipName[CurrentTip][i]);
        }
        u8g2.setCursor(12 * digit, 32 + SCREEN_OFFSET);
        u8g2.print(char(value));
      } while (u8g2.nextPage());
      if (lastbutton && digitalRead(BUTTON_PIN)) {
        delay(10);
        lastbutton = false;
      }
    } while (digitalRead(BUTTON_PIN) || lastbutton);
    TipName[CurrentTip][digit] = value;
    beep();
    delay(10);
  }
  TipName[CurrentTip][TIPNAMELENGTH - 1] = 0;
}

void DeleteTipScreen() {
  if (NumberOfTips == 1) {
    MessageScreen(DeleteMessage, ARRAY_COUNT(DeleteMessage));
  } else if (MenuScreen(SureItems, ARRAY_COUNT(SureItems), 0)) {
    if (CurrentTip == (NumberOfTips - 1)) {
      CurrentTip--;
    } else {
      for (uint8_t i = CurrentTip; i < (NumberOfTips - 1); i++) {
        for (uint8_t j = 0; j < TIPNAMELENGTH; j++) TipName[i][j] = TipName[i + 1][j];
        for (uint8_t j = 0; j < 4; j++) CalTemp[i][j] = CalTemp[i + 1][j];
      }
    }
    NumberOfTips--;
  }
}

void AddTipScreen() {
  if (NumberOfTips < TIPMAX) {
    CurrentTip = NumberOfTips++;
    InputNameScreen();
    CalTemp[CurrentTip][0] = TEMP200;
    CalTemp[CurrentTip][1] = TEMP280;
    CalTemp[CurrentTip][2] = TEMP360;
    CalTemp[CurrentTip][3] = TEMPCHP;
  } else MessageScreen(MaxTipMessage, ARRAY_COUNT(MaxTipMessage));
}

uint16_t denoiseAnalog() {
  uint32_t result = 0;
  int resultArray[8];
  for (uint8_t i = 0; i < 8; i++) {
    float raw_adc = adc_sensor.readMiliVolts();
    resultArray[i] = constrain(0.5378f * raw_adc + 6.3959f, 20.0f, 1000.0f);
  }
  for (uint8_t i = 0; i < 8; i++) {
    for (uint8_t j = i + 1; j < 8; j++) {
      if (resultArray[i] > resultArray[j]) {
        int temp = resultArray[i];
        resultArray[i] = resultArray[j];
        resultArray[j] = temp;
      }
    }
  }
  for (uint8_t i = 2; i < 6; i++) result += resultArray[i];
  return (result / 4);
}

double getChipTemp() {
#if defined(LIS)
  if (!accelPresent) return TEMPCHP;
  return accel.getTemperature();
#else
  #error "No temperature/IMU sensor type defined"
#endif
}

uint16_t getVIN() {
  long result = 0;
  for (uint8_t i = 0; i < 4; i++) result += adc_vin.readMiliVolts();
  return ((result / 4) * 31.3f);
}

uint32_t btnNPressMs = 0, btnNLastStepMs = 0;
uint32_t btnPPressMs = 0, btnPLastStepMs = 0;
const uint32_t BUTTON_REPEAT_DELAY_MS = 400;
const uint32_t BUTTON_REPEAT_RATE_MS = 80;

void resetButtonRepeat() {
  uint32_t now = millis();
  btnNPressMs = now;
  btnNLastStepMs = now;
  btnPPressMs = now;
  btnPLastStepMs = now;
}

void Button_loop() {
  uint32_t now = millis();

  // Pin 2 (-) Decrement / Step down
  if (!digitalRead(BUTTON_N_PIN) && a0 == 1) {
    delay(BUTTON_DELAY);
    if (!digitalRead(BUTTON_N_PIN)) {
      int count0 = count;
      count = constrain(count + countStep, countMin, countMax);
      if (!(countMin == TEMP_MIN && countMax == TEMP_MAX)) {
        if (count0 + countStep > countMax) count = countMin;
      }
      a0 = 0;
      now = millis();
      btnNPressMs = now;
      btnNLastStepMs = now;
    }
  } else if (!digitalRead(BUTTON_N_PIN) && a0 == 0) {
    if (((uint32_t)(now - btnNPressMs) >= BUTTON_REPEAT_DELAY_MS) &&
        ((uint32_t)(now - btnNLastStepMs) >= BUTTON_REPEAT_RATE_MS)) {
      count = constrain(count + countStep, countMin, countMax);
      btnNLastStepMs = now;
    }
  } else if (digitalRead(BUTTON_N_PIN)) {
    a0 = 1;
    btnNPressMs = now;
    btnNLastStepMs = now;
  }

  // Pin 4 (+) Increment / Step up
  if (!digitalRead(BUTTON_P_PIN) && b0 == 1) {
    delay(BUTTON_DELAY);
    if (!digitalRead(BUTTON_P_PIN)) {
      int count0 = count;
      count = constrain(count - countStep, countMin, countMax);
      if (!(countMin == TEMP_MIN && countMax == TEMP_MAX)) {
        if (count0 - countStep < countMin) count = countMax;
      }
      b0 = 0;
      now = millis();
      btnPPressMs = now;
      btnPLastStepMs = now;
    }
  } else if (!digitalRead(BUTTON_P_PIN) && b0 == 0) {
    if (((uint32_t)(now - btnPPressMs) >= BUTTON_REPEAT_DELAY_MS) &&
        ((uint32_t)(now - btnPLastStepMs) >= BUTTON_REPEAT_RATE_MS)) {
      count = constrain(count - countStep, countMin, countMax);
      btnPLastStepMs = now;
    }
  } else if (digitalRead(BUTTON_P_PIN)) {
    b0 = 1;
    btnPPressMs = now;
    btnPLastStepMs = now;
  }
}

void PD_Update() {
  switch (VoltageValue) {
    case 0: digitalWrite(PD_CFG_0, LOW); digitalWrite(PD_CFG_1, LOW); digitalWrite(PD_CFG_2, LOW); break;
    case 1: digitalWrite(PD_CFG_0, LOW); digitalWrite(PD_CFG_1, LOW); digitalWrite(PD_CFG_2, HIGH); break;
    case 2: digitalWrite(PD_CFG_0, LOW); digitalWrite(PD_CFG_1, HIGH); digitalWrite(PD_CFG_2, HIGH); break;
    case 3: digitalWrite(PD_CFG_0, LOW); digitalWrite(PD_CFG_1, HIGH); digitalWrite(PD_CFG_2, LOW); break;
    case 4: digitalWrite(PD_CFG_0, LOW); digitalWrite(PD_CFG_1, HIGH); digitalWrite(PD_CFG_2, LOW); break;
    default: break;
  }

  ledcSetup(CONTROL_CHANNEL, CONTROL_FREQ, CONTROL_RES);
  ledcAttachPin(CONTROL_PIN, CONTROL_CHANNEL);
  ledcWrite(CONTROL_CHANNEL, HEATER_OFF);
}

static void usbEventCallback(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_base == ARDUINO_FIRMWARE_MSC_EVENTS) {
    switch (event_id) {
      case ARDUINO_FIRMWARE_MSC_START_EVENT: mscDisplayState = MSC_UI_START; break;
      case ARDUINO_FIRMWARE_MSC_WRITE_EVENT: mscDisplayState = MSC_UI_WRITING; break;
      case ARDUINO_FIRMWARE_MSC_END_EVENT:   mscDisplayState = MSC_UI_END; break;
      case ARDUINO_FIRMWARE_MSC_ERROR_EVENT: mscDisplayState = MSC_UI_ERROR; break;
      case ARDUINO_FIRMWARE_MSC_POWER_EVENT: mscDisplayState = MSC_UI_POWER; break;
      default: break;
    }
  }
}
