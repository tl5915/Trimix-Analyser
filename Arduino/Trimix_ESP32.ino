#include <WiFi.h>
#include <Update.h>
#include <EEPROM.h>
#include <esp_bt.h>
#include <esp_wifi.h>
#include <WebServer.h>
#include <Adafruit_GFX.h>
#include <Adafruit_DS3502.h>
#include <Adafruit_SH110X.h>
#include <Adafruit_ADS1X15.h>
#include <SparkFun_SGP40_Arduino_Library.h>
#include <sensirion_arch_config.h>
#include <sensirion_voc_algorithm.h>

SGP40 sgp40;
Adafruit_DS3502 ds3502;
Adafruit_ADS1115 ads;
Adafruit_SH1106G display(128, 64, &Wire);

// Pin Definition
const uint8_t SCLPin = D6;        // GPIO21, SCL
const uint8_t SDAPin = D5;        // GPIO 7, SDA
const uint8_t ADDRPin = D4;       // GPIO 6, ADS1115 Address
const uint8_t powerPin = D3;      // GPIO 5, Power off
const uint8_t batteryPin = A2;    // GPIO 4, Battery monitoring
const uint8_t MD62VccPin = A1;    // GPIO 3, MD62 input voltage monitoring
const uint8_t calButtonPin = D0;  // GPIO 2, Calibration button

// EEPROM Address
const int ADDR_HELIUM_POLARITY = 0;
const int ADDR_WIPER_VALUE = 4;
const int ADDR_OXYGEN_CAL_PERCENTAGE = 8;
const int ADDR_HELIUM_CAL_PERCENTAGE = 12;
const int ADDR_OXYGEN_CAL_VOLTAGE = 16;
const int ADDR_PURE_OXYGEN_VOLTAGE = 24;
const int ADDR_HELIUM_CAL_VOLTAGE = 32;
const int ADDR_WIFI_STATUS = 40;

// Calibration
const uint8_t defaultwiperValue = 57;           // Potentiometer wiper position
const uint8_t defaultOxygenCalPercentage = 99;  // Oxygen calibration percentage
const uint8_t defaultHeliumCalPercentage = 67;  // Helium calibration percentage
const float defaultOxygenCalVoltage = 9.38;     // Oxygen voltage in air
const float defaultPureOxygenVoltage = 49.75;     // Oxygen voltage in oxygen
const float defaultHeliumCalVoltage = 382.01;    // Helium voltage in helium
bool heliumPolarity = false;
bool isTwoPointCalibrated = false;
uint8_t wiperValue = defaultwiperValue;
uint8_t bestWiperValue = wiperValue;
uint8_t OxygenCalPercentage = defaultOxygenCalPercentage; 
uint8_t HeliumCalPercentage = defaultHeliumCalPercentage;
float oxygencalVoltage = defaultOxygenCalVoltage;
float pureoxygenVoltage = defaultPureOxygenVoltage;
float heliumcalVoltage = defaultHeliumCalVoltage;

// Sampling
const uint8_t calibrationSampleCount = 20;  // Average 20 samples for calibration
const uint8_t samplingRateHz = 50;          // Sampling rate 50 Hz
const uint8_t displayRateHz = 2;            // Display refresh rate 2 Hz
unsigned long lastSampleTime = 0;
unsigned long lastDisplayUpdate = 0;
uint16_t voc = 0;
uint16_t sgpRaw = 0;
uint16_t sgpRawCorr = 0;
uint16_t sampleCount = 0;
uint16_t avgSampleCount = 0;
float oxygenVoltage = 0.0;
float oxygenSum = 0.0;
float avgOxygenVoltage = 0.0;
float oxygenPercentage = 0.0;
float heliumVoltage = 0.0;
float heliumSum = 0.0;
float avgHeliumVoltage = 0.0;
float correctedHeliumVoltage = 0.0;
float heliumPercentage = 0.0;
float MD62Voltage = 0.0;
float MD62Sum = 0.0;
float avgMD62Voltage = 0.0;
float batteryVoltage = 0.0;
float batterySum = 0.0;
float avgBatteryVoltage = 0.0;
uint8_t batteryPercentage = 0;
uint16_t mod14 = 0;
uint16_t mod16 = 0;
uint16_t end = 0;
float den = 0.0;
bool sixnine = false;
unsigned long sixnineStartTime = 0;

// Calibration Button
const unsigned long debounceDelay = 50;        // Button debounce delay 50 ms
const unsigned long decisionWindow = 200;      // Short press threshold 200 ms
const unsigned long longPressDuration = 800;   // Long press threshold 800 ms
const unsigned long powerOffDuration = 3000;   // Power off threshold 3000 ms
const unsigned long powerOnDuration = 1000;    // Power on threshold 1000 ms
const unsigned long timeOutDuration = 900000;  // Power time out threshold 15 min
unsigned long lastButtonDebounceTime = 0;
unsigned long buttonPressStartTime = 0;
unsigned long lastButtonPressTime = 0;
bool isButtonPressed = false;
bool isShortPress = false;
bool isLongPress = false;
bool isPowerOff = false;
bool lastButtonState = false;

// WiFi Settings
const char *ssid = "trimix_analyser";  // WiFi SSID
const char *password = "12345678";     // WiFi password
WebServer server(80);                  // Web server on port 80
bool wifiEnabled = false;              // WiFi default off


// Calibration Button State
void checkCalibrationButton() {
  unsigned long currentTime = millis();
  bool currentButtonState = digitalRead(calButtonPin) == LOW;

  if (currentButtonState != lastButtonState) {
    lastButtonDebounceTime = currentTime;  // Reset debounce timer
  }

  if ((currentTime - lastButtonDebounceTime) > debounceDelay) {  // Button debounce
    if (currentButtonState) {  // Button pressed
      if (!isButtonPressed) {  // New press detected
        buttonPressStartTime = currentTime;
        isButtonPressed = true;
        isShortPress = false;
        isLongPress = false;
        isPowerOff = false;
      } else if (!isLongPress && (currentTime - buttonPressStartTime >= longPressDuration)) {  // Long press
        isShortPress = false;
        isLongPress = true;
        isPowerOff = false;
      } else if (!isPowerOff && (currentTime - buttonPressStartTime >= powerOffDuration)) {  // Power off
        isShortPress = false;
        isLongPress = false;
        isPowerOff = true;
        powerOffMode();
      }
    } else {  // Button is released
      if (isButtonPressed) {
        isButtonPressed = false;
        if (!isLongPress && (currentTime - buttonPressStartTime) <= decisionWindow) {  // Short press
          isShortPress = true;
        }
      }
    }
  }
  lastButtonState = currentButtonState;
}

// Calibration Mode
uint8_t calibrationOption = 0;
bool inCalibrationMode = false;
const uint8_t numCalibrationOptions = 8;
String getCalibrationOption(uint8_t index) {
  switch (index) {
    case 0: return "21% O2";
    case 1: return "0% He";
    case 2: return String(OxygenCalPercentage) + "% O2";
    case 3: return String(HeliumCalPercentage) + "% He";
    case 4: return "View";
    case 5: return "Reset";
    case 6: return "Setting";
    case 7: return "Exit";
    default: return "";
  }
}

void enterCalibrationMode() {
  isShortPress = false;
  isLongPress = false;
  inCalibrationMode = true;
  calibrationOption = 0;

  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(10, 24);
  display.print(F("Calibrate"));
  display.display();
  delay(500);

  while (inCalibrationMode) {
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(10, 0);
    display.print(F("Calibrate"));
    display.drawLine(0, 18, 128, 18, SH110X_WHITE);

    display.setTextSize(1);
    for (uint8_t i = 0; i < numCalibrationOptions; i++) {
      if (i < 4) {  // Left column
        display.setCursor(0, 24 + (i * 10));
      } else {      // Right column
        display.setCursor(64, 24 + ((i - 4) * 10));
      }
      if (i == calibrationOption) {
        display.print(F("> "));
      } else {
        display.print(F("  "));
      }
      display.print(getCalibrationOption(i));
    }
    display.display();

    checkCalibrationButton();

    if (isShortPress) {        // Short press to move to next option
      isShortPress = false;
      calibrationOption = (calibrationOption + 1) % numCalibrationOptions;
    } else if (isLongPress) {  // Long press to select option
      isLongPress = false;
      switch (calibrationOption) {
        case 0:
          airOxygenCalibration();       // Perform 21% oxygen calibration
          break;
        case 1:
          zeroHeliumCalibration();      // Perform 0% helium calibration
          break;
        case 2:
          pureOxygenCalibration();      // Perform 100% oxygen calibration
          break;
        case 3:
          performHeliumCalibration();   // Perform 100% helium calibration
          break;
        case 4:
          calibrationDisplay();         // View calibration values
          break;
        case 5:
          resetToDefaultCalibration();  // Reset all calibrations
          break;
        case 6:
          enterSettingMode();           // Enter setting mode
          break;
        case 7:
          inCalibrationMode = false;    // Exit calibration mode
          break;
      }
    }
  }
}

// Calibration Mode - 21% Oxygen Calibration
void airOxygenCalibration() {
  while (true) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(8, 18);
    display.print(F("21% Oxygen"));
    display.setCursor(8, 38);
    display.print(F("Calibrating..."));
    display.display();

    oxygenSum = 0.0;
    for (uint8_t i = 0; i < calibrationSampleCount; i++) {
      oxygenVoltage = getOxygenVoltage();
      oxygenSum += oxygenVoltage;
      delay(20);  // 50 Hz sampling rate
    }
    oxygencalVoltage = oxygenSum / calibrationSampleCount;

    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(8, 18);
    display.print(F("21% O2 Calibration"));
    display.setCursor(8, 38);
    display.print(oxygencalVoltage, 2);
    display.print(F(" mV"));
    display.display();

    while (true) {
      checkCalibrationButton();

      if (isShortPress) {        // Short press to repeat calibration
        isShortPress = false;
        break; 
      } else if (isLongPress) {  // Long press to save calibration
        isLongPress = false;

        EEPROM.put(ADDR_OXYGEN_CAL_VOLTAGE, oxygencalVoltage);
        EEPROM.commit();

        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(8, 28);
        display.print(F("Calibration Saved"));
        display.display();
        delay(500);

        inCalibrationMode = false;
        return;  // Exit calibration after saving
      }
    }
  }
}

// Calibration Mode - 100% Oxygen Calibration
void pureOxygenCalibration() {
  while (true) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(8, 18);
    display.print(OxygenCalPercentage);
    display.print(F("% Oxygen"));
    display.setCursor(8, 38);
    display.print(F("Calibrating..."));
    display.display();

    oxygenSum = 0.0;
    for (uint8_t i = 0; i < calibrationSampleCount; i++) {
      oxygenVoltage = getOxygenVoltage();
      oxygenSum += oxygenVoltage;
      delay(20);  // 50 Hz sampling rate
    }
    pureoxygenVoltage = oxygenSum / calibrationSampleCount;

    if (pureoxygenVoltage <= oxygencalVoltage) {  // Invalid calibration
      while (true) {
        display.clearDisplay();
        display.setTextSize(2);
        display.setCursor(28, 20);
        display.print(F("Failed"));
        display.setTextSize(1);
        display.setCursor(22, 54);
        display.print(F("O2 Voltage Low"));
        display.display();

        checkCalibrationButton();
    
        if (isShortPress) {        // Short press to repeat calibration
          isShortPress = false;
          break;
        } else if (isLongPress) {  // Long press to exit without saving
          isLongPress = false;
          inCalibrationMode = false;
          return;
        }
      }
    } else {  // Valid calibration
      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(8, 18);
      display.print(OxygenCalPercentage);
      display.print(F("% O2 Calibration"));
      display.setCursor(8, 38);
      display.print(pureoxygenVoltage, 2);
      display.print(F(" mV"));
      display.display();
      
      while (true) {
        checkCalibrationButton();

        if (isShortPress) {        // Short press to repeat calibration
          isShortPress = false;
          break;
        } else if (isLongPress) {  // Long press to save calibration
          isLongPress = false;

          EEPROM.put(ADDR_PURE_OXYGEN_VOLTAGE, pureoxygenVoltage);
          EEPROM.commit();
          isTwoPointCalibrated = true;

          display.clearDisplay();
          display.setTextSize(1);
          display.setCursor(8, 28);
          display.print(F("Calibration Saved"));
          display.display();
          delay(500);

          inCalibrationMode = false;
          return;  // Exit calibration after saving
        }
      }
    }
  }
}

// Calibration Mode - 0% Helium Calibration
void zeroHeliumCalibration() {
  while (true) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(8, 18);
    display.print(F("0% Helium"));
    display.setCursor(8, 38);
    display.print(F("Calibrating..."));
    display.display();

    uint8_t lowerLimit = 0;    // Resistor low end
    uint8_t upperLimit = 127;  // Resistor high end

    ds3502.setWiper(lowerLimit);
    delay(50);  // 50 ms delay for voltage stabilisation
    float voltageAtMin = getHeliumVoltage();
    ds3502.setWiper(upperLimit);
    delay(50);  // 50 ms delay for voltage stabilisation
    float voltageAtMax = getHeliumVoltage();
    bool potInverted = voltageAtMax < voltageAtMin;  // Determine polarity of DS3502

    float heliumZeroVoltage = 999.9;
    unsigned long calibrationStartTime = millis();

    while (lowerLimit <= upperLimit) {
      if (millis() - calibrationStartTime > 10000) {  // Time out after 10 seconds
        display.clearDisplay();
        display.setTextSize(2);
        display.setCursor(28, 24);
        display.print(F("Failed"));
        display.display();
        while (true) {
          checkCalibrationButton();
          if (isShortPress) {        // Short press to repeat calibration
            isShortPress = false;
            goto restartZeroHeCalibration;
          } else if (isLongPress) {  // Long press to exit without saving
            isLongPress = false;
            EEPROM.get(ADDR_WIPER_VALUE, bestWiperValue);
            ds3502.setWiper(bestWiperValue);
            inCalibrationMode = false;
            return;
          }
        }
      }

      wiperValue = (lowerLimit + upperLimit) / 2;  // Start from mid-point
      ds3502.setWiper(wiperValue);
      delay(50);  // 50 ms delay for voltage stabilisation

      heliumSum = 0.0;
      for (uint8_t i = 0; i < calibrationSampleCount; i++) {
        heliumVoltage = getHeliumVoltage();
        heliumSum += heliumVoltage;
        delay(20);  // 50 Hz sampling rate
      }
      avgHeliumVoltage = heliumSum / calibrationSampleCount;
      float currentHeliumZeroVoltage = avgHeliumVoltage - 0.62;  // Helium correction factor at 21% O2
      
      if (currentHeliumZeroVoltage > 0 && fabs(currentHeliumZeroVoltage) < fabs(heliumZeroVoltage)) {
        heliumZeroVoltage = currentHeliumZeroVoltage;
        bestWiperValue = wiperValue;  // Find wiper position that gives lowest positive corrected helium voltage
      }

      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(43, 0);
      display.print(F("Current"));
      display.setCursor(8, 10);
      display.print(F("Position: "));
      display.print(wiperValue);
      display.print(F(" / 127"));
      display.setCursor(8, 20);
      display.print(F(" Voltage: "));
      display.print(currentHeliumZeroVoltage, 2);
      display.print(F(" mV"));
      display.setCursor(52, 36);
      display.print(F("Best"));
      display.setCursor(8, 46);
      display.print(F("Position: "));
      display.print(bestWiperValue);
      display.print(F(" / 127"));
      display.setCursor(8, 56);
      display.print(F(" Voltage: "));
      display.print(heliumZeroVoltage, 2);
      display.print(F(" mV"));
      display.display();

      if (!potInverted) {  // Binary search with inverted DS3502 polarity
        if (avgHeliumVoltage <= 0.62) {
          lowerLimit = wiperValue + 1;
        } else {
          upperLimit = wiperValue - 1;
        }
      } else {             // Binary search with default DS3502 polarity
        if (avgHeliumVoltage <= 0.62) {
          upperLimit = wiperValue - 1;
        } else {
          lowerLimit = wiperValue + 1;
        }
      }
    }

    ds3502.setWiper(bestWiperValue);

    while (true) {
      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(8, 6);
      display.print(F("0% He Calibration"));
      display.setCursor(8, 22);
      display.print(heliumZeroVoltage, 2);
      display.print(F(" mV"));
      display.setCursor(8, 34);
      display.print(F("Pot: "));
      display.print(bestWiperValue);
      display.print(F(" / 127"));
      display.drawRect(4, 48, 120, 16, SH110X_WHITE);  // Potentiometer
      int16_t midpoint = map(bestWiperValue, 0, 127, 0, 116);  // Centre position
      display.fillRect((midpoint - 1), 50, 3, 12, SH110X_WHITE);  // Wiper
      display.display();

      checkCalibrationButton();

      if (isShortPress) {        // Short press to repeat calibration
        isShortPress = false;
        break;
      } else if (isLongPress) {  // Long press to save calibration
        isLongPress = false;

        EEPROM.put(ADDR_WIPER_VALUE, bestWiperValue);
        EEPROM.commit();

        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(8, 28);
        display.print(F("Calibration Saved"));
        display.display();
        delay(500);

        inCalibrationMode = false;
        return;  // Exit calibration after saving
      }
    }
restartZeroHeCalibration:
    ;
  }
}

// Calibration Mode - 100% Helium Calibration
void performHeliumCalibration() {
  while (true) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(8, 18);
    display.print(HeliumCalPercentage);
    display.print(F("% Helium"));
    display.setCursor(8, 38);
    display.print(F("Calibrating..."));
    display.display();

    heliumSum = 0.0;
    for (uint8_t i = 0; i < calibrationSampleCount; i++) {
      heliumVoltage = getHeliumVoltage();
      heliumSum += heliumVoltage;
      delay(20);  // 20 ms delay between samples
    }
    heliumcalVoltage = (heliumSum / calibrationSampleCount) - (17.0 / (1 + exp(0.105 * (0.3240 * HeliumCalPercentage + 19.455))));  // Calibration factor based on standard gas

    if (heliumcalVoltage < 50.0) {  // 50 mV threshold for invalid helium calibration
      while (true) {
        display.clearDisplay();
        display.setTextSize(2);
        display.setCursor(28, 20);
        display.print(F("Failed"));
        display.setTextSize(1);
        display.setCursor(22, 54);
        display.print(F("He Voltage Low"));
        display.display();

        checkCalibrationButton();
    
        if (isShortPress) {        // Short press to repeat calibration
          isShortPress = false;
          break;
        } else if (isLongPress) {  // Long press to exit without saving
          isLongPress = false;
          inCalibrationMode = false;
          return;
        }
      }
    } else {  // Valid calibration
      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(8, 18);
      display.print(HeliumCalPercentage);
      display.print(F("% He Calibration"));
      display.setCursor(8, 38);
      display.print(heliumcalVoltage, 2);
      display.print(F(" mV"));
      display.display();
      
      while (true) {
        checkCalibrationButton();

        if (isShortPress) {        // Short press to repeat calibration
          isShortPress = false;
          break;
        } else if (isLongPress) {  // Long press to save calibration
          isLongPress = false;

          EEPROM.put(ADDR_HELIUM_CAL_VOLTAGE, heliumcalVoltage);
          EEPROM.commit();

          display.clearDisplay();
          display.setTextSize(1);
          display.setCursor(8, 28);
          display.print(F("Calibration Saved"));
          display.display();
          delay(500);

          inCalibrationMode = false;
          return;  // Exit calibration after saving
        }
      }
    }
  }
}

// Display Calibration Values
void calibrationDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(13, 0);
  if (isTwoPointCalibrated) {
    display.print(F("2-pt Caliberation"));
  } else {
    display.print(F("1-pt Caliberation"));
  }
  display.setCursor(8, 12);
  display.print(F("21% O2: "));
  display.print(oxygencalVoltage, 2);
  display.print(F(" mV"));
  display.setCursor(8, 22);
  display.print(OxygenCalPercentage);
  display.print(F("% O2: "));
  display.print(pureoxygenVoltage, 2);
  display.print(F(" mV"));
  display.setCursor(8, 32);
  display.print(HeliumCalPercentage);
  display.print(F("% He: "));
  display.print(heliumcalVoltage, 2);
  display.print(F(" mV"));
  display.setCursor(26, 44);
  display.print(F("Pot: "));
  display.print(bestWiperValue);
  display.print(F(" / 128"));
  display.setCursor(0, 56);
  display.print(F("Vcc:"));
  display.print(avgMD62Voltage, 2);
  display.print(F("V"));
  display.setCursor(74, 56);
  display.print(F("Bat:"));
  display.print(avgBatteryVoltage, 2);
  display.print(F("V"));
  display.display();
  delay(2000);
}

// Calibration Mode - Reset to Default
void resetToDefaultCalibration() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(34, 12);
  display.print(F("Reset"));
  display.display();
  delay(500);

  bool confirmReset = false;

  while (true) {
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(34, 12);
    display.print(F("Reset"));
    if (confirmReset) {
      display.setCursor(22, 36);
      display.print(F("Confirm"));
    } else {
      display.setCursor(28, 36);
      display.print(F("Cancel"));
    }
    display.display();

    checkCalibrationButton();

    if (isShortPress) {        // Short press to change option
      isShortPress = false;
      confirmReset = !confirmReset;
    } else if (isLongPress) {  // Long press to confirm
      isLongPress = false;

      if (!confirmReset) {
        display.clearDisplay();
        display.setTextSize(2);
        display.setCursor(40, 24);
        display.print(F("Exit"));
        display.display();
        delay(500);
        return;  // Exit
      } else {
        EEPROM.put(ADDR_HELIUM_POLARITY, false);
        EEPROM.put(ADDR_WIPER_VALUE, defaultwiperValue);
        EEPROM.put(ADDR_OXYGEN_CAL_PERCENTAGE, defaultOxygenCalPercentage);
        EEPROM.put(ADDR_HELIUM_CAL_PERCENTAGE, defaultHeliumCalPercentage);
        EEPROM.put(ADDR_OXYGEN_CAL_VOLTAGE, defaultOxygenCalVoltage);
        EEPROM.put(ADDR_PURE_OXYGEN_VOLTAGE, defaultPureOxygenVoltage);
        EEPROM.put(ADDR_HELIUM_CAL_VOLTAGE, defaultHeliumCalVoltage);
        EEPROM.put(ADDR_WIFI_STATUS, false);
        EEPROM.commit();

        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(8, 18);
        display.print(F("Reset Calibration"));
        display.setCursor(8, 38);
        display.print(F("Rebooting..."));
        display.display();
        delay(500);

        esp_restart();  // Restart after reset
      }
    }
  }
}

// Setting Mode
uint8_t settingOption = 0;
bool inSettingMode = false;
const uint8_t numSettingOptions = 5;
const char* settingOptions[] = {
  "O2 Calib %",
  "He Calib %",
  "He Polarity",
  "WiFi",
  "Exit"
};

void enterSettingMode() {
  inSettingMode = true;
  isShortPress = false;
  isLongPress = false;
  settingOption = 0;

  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(22, 24);
  display.print(F("Setting"));
  display.display();
  delay(500);

  while (inSettingMode) {
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(22, 0);
    display.print(F("Setting"));
    display.drawLine(0, 18, 128, 18, SH110X_WHITE);

    display.setTextSize(1);
    for (uint8_t i = 0; i < numSettingOptions; i++) {
      if (i == numSettingOptions - 1) {
        display.setCursor(92, 54);
      } else {
        display.setCursor(0, 24 + (i * 10));
      }
      if (i == settingOption) {
        display.print(F("> "));
      } else {
        display.print(F("  "));
      }
      display.print(settingOptions[i]);
    }
    display.display();

    checkCalibrationButton();

    if (isShortPress) {        // Short press to move to next option
      isShortPress = false;
      settingOption = (settingOption + 1) % numSettingOptions;
    } else if (isLongPress) {  // Long press to select option
      isLongPress = false;
      switch (settingOption) {
        case 0:
          setOxygenCalibration();  // Set oxygen calibration percentage
          break;
        case 1:
          setHeliumCalibration();  // Set helium calibration percentage
          break;
        case 2:
          setHeliumPolarity();     // Set helium sensor polarity
          break;
        case 3:
          setWiFi();               // Set WiFi mode
          break;
        case 4:
          isShortPress = false;
          isLongPress = false;
          inSettingMode = false;
          display.clearDisplay();
          display.setTextSize(2);
          display.setCursor(40, 24);
          display.print(F("Exit"));
          display.display();
          delay(500);
          return;                  // Exit setting mode
      }
    }
  }
}

// Setting Mode - Oxygen Calibration Percentage
void setOxygenCalibration() {
  isShortPress = false;
  isLongPress = false;
  bool isExitMode = false;

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(55, 0);
  display.print(F("..."));
  display.setCursor(40, 12);
  display.print(F("O2 Cal %"));
  display.setTextSize(2);
  display.setCursor(46, 30);
  display.print(OxygenCalPercentage);
  display.print(F("%"));
  display.display();
  delay(500);

  while (true) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(40, 12);
    display.print(F("O2 Cal %"));
    display.setTextSize(2);
    if (isExitMode) {
      display.setCursor(40, 30);
      display.print(F("Exit"));
    } else {
      display.setCursor(46, 30);
      display.print(OxygenCalPercentage);
      display.print(F("%"));
    }
    display.display();

    checkCalibrationButton();

    if (isShortPress) {        // Short press to decrease percentage
      isShortPress = false;
      if (OxygenCalPercentage > 40) {  // Minimum 40% oxygen
        OxygenCalPercentage--;
        isExitMode = false;
      } else {
        if (!isExitMode) {
          isExitMode = true;
        } else {
          OxygenCalPercentage = 99;
          isExitMode = false;
        }
      }
    } else if (isLongPress) {  // Long press to save the setting
      isLongPress = false;
      if (isExitMode) {
        display.clearDisplay();
        display.setTextSize(2);
        display.setCursor(40, 24);
        display.print(F("Exit"));
        display.display();
        delay(500);
        return;
      } else {
        EEPROM.put(ADDR_OXYGEN_CAL_PERCENTAGE, OxygenCalPercentage);
        EEPROM.commit();

        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(8, 18);
        display.print(F("O2 Cal % Saved:"));
        display.setCursor(8, 38);
        display.print(OxygenCalPercentage);
        display.print(F("%"));
        display.display();
        delay(500);

        esp_restart();  // Restart after setting saved
      }
    }
  }
}

// Setting Mode - Helium Calibration Percentage
void setHeliumCalibration() {
  isShortPress = false;
  isLongPress = false;
  bool isExitMode = false;

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(55, 0);
  display.print(F("..."));
  display.setCursor(40, 12);
  display.print(F("He Cal %"));
  display.setTextSize(2);
  display.setCursor(46, 30);
  display.print(HeliumCalPercentage);
  display.print(F("%"));
  display.display();
  delay(500);

  while (true) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(40, 12);
    display.print(F("He Cal %"));
    display.setTextSize(2);
    if (isExitMode) {
      display.setCursor(40, 30);
      display.print(F("Exit"));
    } else {
      display.setCursor(46, 30);
      display.print(HeliumCalPercentage);
      display.print(F("%"));
    }
    display.display();

    checkCalibrationButton();

    if (isShortPress) {        // Short press to decrease percentage
      isShortPress = false;
      if (HeliumCalPercentage > 30) {  // Minimum 30% oxygen
        HeliumCalPercentage--;
        isExitMode = false;
      } else {
        if (!isExitMode) {
          isExitMode = true;
        } else {
          HeliumCalPercentage = 99;
          isExitMode = false;
        }
      } 
    } else if (isLongPress) {  // Long press to save setting
      isLongPress = false;
      if (isExitMode) {
        display.clearDisplay();
        display.setTextSize(2);
        display.setCursor(40, 24);
        display.print(F("Exit"));
        display.display();
        delay(500);
        return;
      } else {
        EEPROM.put(ADDR_HELIUM_CAL_PERCENTAGE, HeliumCalPercentage);
        EEPROM.commit();

        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(8, 18);
        display.print(F("He Cal % Saved:"));
        display.setCursor(8, 38);
        display.print(HeliumCalPercentage);
        display.print(F("%"));
        display.display();
        delay(500);

        esp_restart();  // Restart after setting saved
      }
    }
  }
}

// Setting Mode - Helium Sensor Polarity
void setHeliumPolarity() {
  isShortPress = false;
  isLongPress = false;

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(55, 0);
  display.print(F("..."));
  display.setCursor(31, 12);
  display.print(F("He Polarity"));
  display.setTextSize(2);
  display.setCursor(22, 30);
  display.print(heliumPolarity ? F("Reverse") : F("Default"));
  display.display();
  delay(500);

  while (true) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(31, 12);
    display.print(F("He Polarity"));
    display.setTextSize(2);
    display.setCursor(22, 30);
    display.print(heliumPolarity ? F("Reverse") : F("Default"));
    display.display();

    checkCalibrationButton();

    if (isShortPress) {        // Short press to switch helium sensor polarity
      isShortPress = false;
      heliumPolarity = !heliumPolarity;
    } else if (isLongPress) {  // Long press to save setting
      isLongPress = false;

      EEPROM.put(ADDR_HELIUM_POLARITY, heliumPolarity);
      EEPROM.commit();

      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(8, 18);
      display.print(F("He Polarity Saved:"));
      display.setCursor(8, 38);
      display.print(heliumPolarity ? F("Reverse") : F("Default"));
      display.display();
      delay(500);

      esp_restart();  // Restart after setting saved
    }
  }
}

// Setting Mode - WiFi Mode
void setWiFi() {
  isShortPress = false;
  isLongPress = false;

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(55, 0);
  display.print(F("..."));
  display.setCursor(52, 12);
  display.print(F("WiFi"));
  display.setTextSize(2);
  display.setCursor(46, 30);
  display.print(wifiEnabled ? F("On") : F("Off"));
  display.display();
  delay(500);

  while (true) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(52, 12);
    display.print(F("WiFi"));
    display.setTextSize(2);
    display.setCursor(46, 30);
    display.print(wifiEnabled ? F("On") : F("Off"));
    display.display();

    checkCalibrationButton();

    if (isShortPress) {        // Short press to switch helium sensor polarity
      isShortPress = false;
      wifiEnabled = !wifiEnabled;
    } else if (isLongPress) {  // Long press to save setting
      isLongPress = false;

      EEPROM.put(ADDR_WIFI_STATUS, wifiEnabled);
      EEPROM.commit();

      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(8, 18);
      display.print(F("WiFi Mode Saved:"));
      display.setCursor(8, 38);
      display.print(wifiEnabled ? F("On") : F("Off"));
      display.display();
      delay(500);

      esp_restart();  // Restart after setting saved
    }
  }
}

// Power Off
void powerOffMode() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(10, 12);
  display.print(F("Power Off"));
  display.display();
  delay(500);

  bool confirmPowerOff = false;

  while (true) {
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(10, 12);
    display.print(F("Power Off"));
    if (confirmPowerOff) {
      display.setCursor(22, 36);
      display.print(F("Confirm"));
    } else {
      display.setCursor(28, 36);
      display.print(F("Cancel"));
    }
    display.display();

    checkCalibrationButton();

    if (isShortPress) {        // Short press to change option
      isShortPress = false;
      confirmPowerOff = !confirmPowerOff;
    } else if (isLongPress) {  // Long press to confirm
      isLongPress = false;
      if (!confirmPowerOff) {
        return;
      } else {
        digitalWrite(powerPin, HIGH);
      }
    }
  }
}

// Read Oxygen Voltage
float getOxygenVoltage() {
  ads.setGain(GAIN_SIXTEEN);
  int16_t oxygenRaw = ads.readADC_Differential_2_3();  // O2 pins 2 & 3
  return fabs(oxygenRaw * 0.0078125);                  // Gain 16, 256 mV
}

// Read Helium Voltage
float getHeliumVoltage() {
  ads.setGain(GAIN_FOUR);
  int16_t heliumRaw = ads.readADC_Differential_0_1();  // He pins 0 & 1
  if (heliumPolarity) {
    return -(heliumRaw * 0.03125);                     // Apply helium polarity setting
  }
  return heliumRaw * 0.03125;                          // Gain 4, 1024 mV
}

// Oxygen Percentage Calculation
float getOxygenPercentage() {
  if (!isTwoPointCalibrated) {
    return (avgOxygenVoltage / oxygencalVoltage) * 20.9;  // One-point calibration
  }
  return 20.9 + ((avgOxygenVoltage - oxygencalVoltage) / (pureoxygenVoltage - oxygencalVoltage)) * (OxygenCalPercentage - 20.9);  // Two-point calibration
}

// Format Time
String formatTime() {
  unsigned long seconds = millis() / 1000;
  unsigned long minutes = seconds / 60;
  if (minutes > 99) {
    minutes = 99;
    seconds = 59;
  } else {
    seconds = seconds % 60;
  }
  String timeString = "";
  timeString += String(minutes) + ":";
  if (seconds < 10) {
    timeString += "0";
  }
  timeString += String(seconds);
  return timeString;
}

// HTML
const char *htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Trimix Analyser</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <style>
    body { font-family: Arial, sans-serif; text-align: center; margin: 0; padding: 0; background-color: #f4f4f4; }
    .container { padding: 20px; }
    h1 { margin: 0; padding: 20px; background-color: #333; color: white; }
    .info { font-size: 16px; margin: 10px 0; }
    .group { margin-top: 20px; padding: 10px; border: 1px solid #ddd; border-radius: 5px; background-color: #fff; }
    button { padding: 15px 30px; margin-top: 5px; font-size: 20px; }
    input[type="file"] { padding: 10px; margin-top: 10px; }
    .progress-bar { width: 100%; background-color: #ddd; border-radius: 5px; overflow: hidden; margin-top: 10px; }
    .progress { height: 20px; width: 0%; background-color: #4caf50; transition: width 0.4s; }
  </style>
</head>
<body>
  <h1>Trimix Analyser</h1>
  <div class="container">
    <!-- System Data -->
    <div class="info">Power Time: <span id="time">0:00</span></div>
    <div class="info">Battery Voltage: <span id="avgBatteryVoltage">0.0</span> V</div>
    <div class="info">MD62 Vcc Voltage: <span id="avgMD62Voltage">0.0</span> V</div>
    <div class="info">CPU Frequency: <span id="cpuFrequency">0</span> MHz</div>
    <div class="info">WiFi TX Power: <span id="wifiTxPower">0</span> quarter-dBm</div>
    <div class="info">Averaging Sample Count: <span id="count">0</span></div>

    <!-- Gas Quality -->
    <div class="group">
      <h2>Gas Quality</h2>
      <div class="info">VOC Index: <span id="voc">0</span> (1-500)</div>
      <div class="info">VOC Raw: <span id="sgpRawCorr">0</span> (32767-1)</div>
    </div>

    <!-- Oxygen Data -->
    <div class="group">
      <h2>Oxygen</h2>
      <div class="info">Percentage: <span id="oxygen">0.0</span>%</div>
      <div class="info">Voltage: <span id="avgOxygenVoltage">0.0</span> mV</div>
    </div>

    <!-- Helium Data -->
    <div class="group">
      <h2>Helium</h2>
      <div class="info">Percentage: <span id="helium">0.0</span>%</div>
      <div class="info">Raw Voltage: <span id="avgHeliumVoltage">0.0</span> mV</div>
      <div class="info">Corrected Voltage: <span id="correctedHeliumVoltage">0.0</span> mV</div>
    </div>

    <!-- Dive Calculations -->
    <div class="group">
      <h2>Gas Information</h2>
      <div class="info">MOD (ppO2 1.4): <span id="mod14">0</span> m</div>
      <div class="info">MOD (ppO2 1.6): <span id="mod16">0</span> m</div>
      <div class="info">END (@ ppO2 1.4 MOD): <span id="end">0</span> m</div>
      <div class="info">Density (@ ppO2 1.4 MOD): <span id="density">0.0</span> g/L</div>
    </div>

    <!-- Oxygen Calibration -->
    <div class="group">
      <h2>Oxygen Calibration</h2>
      <div class="info">Low Calibration Percentage: 21%</div>
      <div class="info">Low Calibration Voltage: <span id="oxygencalVoltage">0.0</span> mV</div>
      <div class="info">High Calibration Percentage: <span id="OxygenCalPercentage">0</span>%</div>
      <div class="info">High Calibration Voltage: <span id="pureoxygenVoltage">0.0</span> mV</div>
    </div>

    <!-- Helium Calibration -->
    <div class="group">
      <h2>Helium Calibration</h2>
      <div class="info">Potentiometer Position: <span id="bestWiperValue">0</span></div>
      <div class="info">Calibration Percentage: <span id="HeliumCalPercentage">0</span>%</div>
      <div class="info">Calibration Voltage: <span id="heliumcalVoltage">0.0</span> mV</div>
    </div>

    <!-- Manual Calibration Button -->
    <div class="group">
      <button onclick="window.location.href='/manual_calibration'">Manual Calibration</button>
    </div>

    <!-- Firmware Update -->
    <div class="group">
      <h2>Firmware Update</h2>
      <form id="uploadForm" method="POST" action="/update" enctype="multipart/form-data" onsubmit="return checkFile();">
        <input type="file" name="firmware" id="fileInput" accept=".bin" required>
        <input type="submit" value="Upload">
        <div class="progress-bar"><div class="progress" id="progress"></div></div>
        <p id="uploadStatus"></p>
      </form>
    </div>
  </div>

  <script>
    document.getElementById("fileInput").addEventListener("change", function() {
      let fileName = this.files[0]?.name || "No file selected";
      document.getElementById("uploadStatus").textContent = "Selected: " + fileName;
    });

    function checkFile() {
      let fileInput = document.getElementById("fileInput");
      let file = fileInput.files[0];
      if (!file) {
        alert("Select a bin file.");
        return false;
      }
      if (file.name.split('.').pop().toLowerCase() !== "bin") {
        alert("Invalid file type.");
        return false;
      }
      if (file.size > 1900000) {
        alert("File size exceeded 1.9 MB.");
        return false;
      }
      uploadFirmware(file);
      return false;
    }

    function uploadFirmware(file) {
      let xhr = new XMLHttpRequest();
      let formData = new FormData();
      formData.append("firmware", file);

      xhr.upload.onprogress = function(event) {
        let percent = Math.round((event.loaded / event.total) * 100);
        document.getElementById("progress").style.width = percent + "%";
        document.getElementById("uploadStatus").textContent = "Uploading... " + percent + "%";
      };
      xhr.onload = function() {
        if (xhr.status === 200) {
          document.getElementById("uploadStatus").textContent = "Upload complete. Device rebooting...";
        } else if (xhr.status === 413) {
          document.getElementById("uploadStatus").textContent = "File too large! Upload failed.";
        } else {
          document.getElementById("uploadStatus").textContent = "Upload failed!";
        }
      };
      xhr.onerror = function() {
        document.getElementById("uploadStatus").textContent = "Upload error!";
      };
      xhr.open("POST", "/update", true);
      xhr.send(formData);
    }

    setInterval(() => {
      fetch("/data").then(response => response.json()).then(data => {
        document.getElementById("time").textContent = data.time;
        document.getElementById("avgBatteryVoltage").textContent = data.avgBatteryVoltage;
        document.getElementById("avgMD62Voltage").textContent = data.avgMD62Voltage;
        document.getElementById("cpuFrequency").textContent = data.cpuFrequency;
        document.getElementById("wifiTxPower").textContent = data.wifiTxPower;
        document.getElementById("count").textContent = data.count;
        document.getElementById("voc").textContent = data.voc;
        document.getElementById("sgpRawCorr").textContent = data.sgpRawCorr;
        document.getElementById("OxygenCalPercentage").textContent = data.OxygenCalPercentage;
        document.getElementById("oxygencalVoltage").textContent = data.oxygencalVoltage;
        document.getElementById("pureoxygenVoltage").textContent = data.pureoxygenVoltage;
        document.getElementById("HeliumCalPercentage").textContent = data.HeliumCalPercentage;
        document.getElementById("heliumcalVoltage").textContent = data.heliumcalVoltage;
        document.getElementById("bestWiperValue").textContent = data.bestWiperValue;
        document.getElementById("avgOxygenVoltage").textContent = data.avgOxygenVoltage;
        document.getElementById("oxygen").textContent = data.oxygen;
        document.getElementById("avgHeliumVoltage").textContent = data.avgHeliumVoltage;
        document.getElementById("correctedHeliumVoltage").textContent = data.correctedHeliumVoltage;
        document.getElementById("helium").textContent = data.helium;
        document.getElementById("mod14").textContent = data.mod14;
        document.getElementById("mod16").textContent = data.mod16;
        document.getElementById("end").textContent = data.end;
        document.getElementById("density").textContent = data.density;
      });
    }, 1000);
  </script>
</body>
</html>
)rawliteral";

const char *manualCalibrationPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Manual Calibration - Trimix Analyser</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <style>
    body { font-family: Arial, sans-serif; text-align: center; margin: 0; padding: 0; background-color: #f4f4f4; }
    .container { padding: 20px; }
    h1 { margin: 0; padding: 20px; background-color: #333; color: white; }
    .group { margin-top: 20px; padding: 10px; border: 1px solid #ddd; border-radius: 5px; background-color: #fff; }
    .info { font-size: 16px; margin: 10px 0; }
    label { display: inline-block; width: 200px; text-align: right; margin-right: 10px; white-space: nowrap; }
    input { padding: 5px; width: 40px; }
    button { padding: 10px 20px; margin-top: 20px; font-size: 16px; }
  </style>
</head>
<body>
  <h1>Manual Calibration</h1>
  <div class="container">
    <div class="group">
      <h2>Manual Calibration Settings</h2>
      <form action="/save_calibration" method="GET">
        <div class="info">
          <label for="wiperValue">Potentiometer Position:</label>
          <input type="number" id="wiperValue" name="wiperValue" min="0" max="127" value="64">
        </div>
        <div class="info">
          <label for="OxygenCalPercentage">O2 Calibration Percentage:</label>
          <input type="number" id="OxygenCalPercentage" name="OxygenCalPercentage" min="21" max="99" value="99">
        </div>
        <div class="info">
          <label for="HeliumCalPercentage">He Calibration Percentage:</label>
          <input type="number" id="HeliumCalPercentage" name="HeliumCalPercentage" min="30" max="99" value="99">
        </div>
        <div class="info">
          <label for="oxygencalVoltage">O2 Low Calibration Voltage:</label>
          <input type="number" step="0.1" id="oxygencalVoltage" name="oxygencalVoltage" value="11.0">
        </div>
        <div class="info">
          <label for="pureoxygenVoltage">O2 High Calibration Voltage:</label>
          <input type="number" step="0.1" id="pureoxygenVoltage" name="pureoxygenVoltage" value="52.6">
        </div>
        <div class="info">
          <label for="heliumcalVoltage">He Calibration Percentage:</label>
          <input type="number" step="0.1" id="heliumcalVoltage" name="heliumcalVoltage" value="582.0">
        </div>
        <div class="info">
          <button type="submit">Save Calibration</button>
        </div>
      </form>
    </div>
    <div class="info">
      <a href="/">Return to Main Page</a>
    </div>
  </div>
</body>
</html>
)rawliteral";

// Send data to client
void handleData() {
  uint8_t cpuFreq = getCpuFrequencyMhz();
  int8_t txPower = 0;
  esp_wifi_get_max_tx_power(&txPower);

  String json = "{";
  json += "\"time\":\"" + formatTime() + "\",";
  json += "\"avgBatteryVoltage\":\"" + String(avgBatteryVoltage, 2) + "\",";
  json += "\"avgMD62Voltage\":\"" + String(avgMD62Voltage, 2) + "\",";
  json += "\"cpuFrequency\":\"" + String(cpuFreq) + "\",";
  json += "\"wifiTxPower\":\"" + String(txPower) + "\",";
  json += "\"count\":\"" + String(avgSampleCount) + "\",";
  json += "\"voc\":\"" + String(voc) + "\",";
  json += "\"sgpRawCorr\":\"" + String(sgpRawCorr) + "\",";
  json += "\"OxygenCalPercentage\":\"" + String(OxygenCalPercentage) + "\",";
  json += "\"oxygencalVoltage\":\"" + String(oxygencalVoltage, 2) + "\",";
  json += "\"pureoxygenVoltage\":\"" + String(pureoxygenVoltage, 2) + "\",";
  json += "\"HeliumCalPercentage\":\"" + String(HeliumCalPercentage) + "\",";
  json += "\"heliumcalVoltage\":\"" + String(heliumcalVoltage, 2) + "\",";
  json += "\"bestWiperValue\":\"" + String(bestWiperValue) + "\",";
  json += "\"avgOxygenVoltage\":\"" + String(avgOxygenVoltage, 2) + "\",";
  json += "\"oxygen\":\"" + String(getOxygenPercentage(), 1) + "\",";
  json += "\"avgHeliumVoltage\":\"" + String(avgHeliumVoltage, 2) + "\",";
  json += "\"correctedHeliumVoltage\":\"" + String(correctedHeliumVoltage, 2) + "\",";
  json += "\"helium\":\"" + String(heliumPercentage, 1) + "\",";
  json += "\"mod14\":\"" + String(mod14) + "\",";
  json += "\"mod16\":\"" + String(mod16) + "\",";
  json += "\"end\":\"" + String(end) + "\",";
  json += "\"density\":\"" + String(den, 1) + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

// Get manual calibration values from client
void handleManualCalibration() {
  if (server.hasArg("wiperValue") && server.hasArg("OxygenCalPercentage") && server.hasArg("HeliumCalPercentage") &&
      server.hasArg("oxygencalVoltage") && server.hasArg("pureoxygenVoltage") && server.hasArg("heliumcalVoltage")) {

    bestWiperValue = server.arg("wiperValue").toInt();
    OxygenCalPercentage = server.arg("OxygenCalPercentage").toInt();
    HeliumCalPercentage = server.arg("HeliumCalPercentage").toInt();
    oxygencalVoltage = server.arg("oxygencalVoltage").toFloat();
    pureoxygenVoltage = server.arg("pureoxygenVoltage").toFloat();
    heliumcalVoltage = server.arg("heliumcalVoltage").toFloat();

    EEPROM.put(ADDR_WIPER_VALUE, bestWiperValue);
    EEPROM.put(ADDR_OXYGEN_CAL_PERCENTAGE, OxygenCalPercentage);
    EEPROM.put(ADDR_HELIUM_CAL_PERCENTAGE, HeliumCalPercentage);
    EEPROM.put(ADDR_OXYGEN_CAL_VOLTAGE, oxygencalVoltage);
    EEPROM.put(ADDR_PURE_OXYGEN_VOLTAGE, pureoxygenVoltage);
    EEPROM.put(ADDR_HELIUM_CAL_VOLTAGE, heliumcalVoltage);
    EEPROM.commit();

    String response = "<html><body><h1>Calibration Saved!</h1><p>Device is restarting...</p></body></html>";
    server.send(200, "text/html", response);

    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(8, 18);
    display.print(F("Manual Calib Saved"));
    display.setCursor(8, 38);
    display.print(F("Rebooting..."));
    display.display();
    delay(500);

    esp_restart();

  } else {
    server.send(400, "text/html", "<html><body><h1>Error:</h1><p>Missing calibration parameters.</p></body></html>");
  }
}

// QR Code
const unsigned char qrCodeBitmap[] PROGMEM = {
	0xff, 0xff, 0x80, 0xc7, 0xc3, 0x9f, 0xff, 0xf0, 0xff, 0xff, 0x80, 0xc7, 0xc3, 0x9f, 0xff, 0xf0, 
	0xff, 0xff, 0x9f, 0xbc, 0x81, 0x1f, 0xff, 0xf0, 0xe0, 0x03, 0x9f, 0x38, 0x00, 0x1c, 0x00, 0x70, 
	0xe0, 0x03, 0x9f, 0x38, 0x00, 0x1c, 0x00, 0x70, 0xe7, 0xf3, 0x9c, 0x38, 0x33, 0x9c, 0xfe, 0x70, 
	0xe7, 0xf3, 0x9c, 0x38, 0x33, 0x9c, 0xfe, 0x70, 0xe7, 0xf3, 0x9f, 0xe6, 0x3f, 0x9c, 0xfe, 0x70, 
	0xe7, 0xf3, 0x9f, 0xc6, 0x3f, 0x9c, 0xfe, 0x70, 0xe7, 0xf3, 0x9f, 0xc7, 0x7f, 0x9c, 0xfe, 0x70, 
	0xe7, 0xf3, 0x80, 0xc7, 0xc0, 0x1c, 0xfe, 0x70, 0xe7, 0xf3, 0x80, 0xc7, 0xc0, 0x1c, 0xfe, 0x70, 
	0xe0, 0x03, 0x80, 0x01, 0xf0, 0x1c, 0x00, 0x70, 0xe0, 0x03, 0x80, 0x01, 0xf0, 0x1c, 0x00, 0x70, 
	0xff, 0xff, 0x98, 0xc7, 0x71, 0x9f, 0xff, 0xf0, 0xff, 0xff, 0x9c, 0xc6, 0x33, 0x9f, 0xff, 0xf0, 
	0xff, 0xff, 0x9c, 0xc6, 0x73, 0x9f, 0xff, 0xf0, 0x00, 0x00, 0x1f, 0xc1, 0xc3, 0x80, 0x00, 0x00, 
	0x00, 0x00, 0x1f, 0xc1, 0xc3, 0x80, 0x00, 0x00, 0xc0, 0x03, 0x9f, 0x06, 0x7f, 0xfc, 0x3f, 0xc0, 
	0xc0, 0x03, 0x9f, 0x06, 0x3f, 0xfc, 0x3f, 0xc0, 0xc0, 0x23, 0xff, 0x86, 0x3f, 0xf8, 0x3f, 0xc0, 
	0xc0, 0x3c, 0x63, 0xc6, 0x3f, 0xe0, 0xff, 0xc0, 0xe0, 0x3e, 0x63, 0xc6, 0x3f, 0xe0, 0xff, 0xc0, 
	0xfe, 0x03, 0xe3, 0x00, 0x0e, 0x1f, 0x39, 0xf0, 0xfe, 0x03, 0xe3, 0x00, 0x0c, 0x1f, 0x39, 0xf0, 
	0x3e, 0x0e, 0x43, 0xc6, 0x3f, 0x89, 0xf8, 0x70, 0x3e, 0x0c, 0x00, 0xc6, 0x3f, 0x80, 0xf8, 0x30, 
	0x3f, 0x0e, 0x00, 0xe6, 0x7f, 0x80, 0xf8, 0x30, 0x07, 0xf3, 0x9c, 0x39, 0xf3, 0xfc, 0x00, 0x30, 
	0x07, 0xf3, 0x9c, 0x39, 0xf3, 0xfc, 0x00, 0x70, 0xc7, 0xfe, 0x7f, 0xe7, 0xfe, 0x00, 0x01, 0xc0, 
	0xc7, 0xfc, 0x7f, 0xc7, 0xfc, 0x00, 0x01, 0xc0, 0xc7, 0xfe, 0xff, 0xc7, 0xfc, 0x02, 0x11, 0xc0, 
	0xc0, 0x03, 0xe3, 0xc1, 0xfc, 0x63, 0x39, 0xf0, 0xc0, 0x03, 0xe7, 0xc1, 0xfe, 0x63, 0x39, 0xf0, 
	0xc6, 0x00, 0x7f, 0xc7, 0xf3, 0x80, 0xe6, 0x70, 0xc6, 0x00, 0x7f, 0xc7, 0xf3, 0x80, 0xc6, 0x30, 
	0xc0, 0x0f, 0xf7, 0x87, 0x73, 0xff, 0xc6, 0x20, 0xc0, 0x0f, 0xe3, 0x06, 0x33, 0xff, 0xc6, 0x00, 
	0xc0, 0x0f, 0xe7, 0x06, 0x33, 0xff, 0xc6, 0x00, 0x00, 0x00, 0x1f, 0x00, 0x0f, 0x80, 0xc6, 0x00, 
	0x00, 0x00, 0x1f, 0x00, 0x0f, 0x80, 0xe6, 0x00, 0xff, 0xff, 0x80, 0x07, 0xf3, 0x9c, 0xf8, 0x30, 
	0xff, 0xff, 0x80, 0x07, 0xf3, 0x9c, 0xf8, 0x30, 0xff, 0xff, 0x80, 0x1f, 0xf3, 0x98, 0xf8, 0x30, 
	0xe0, 0x03, 0x80, 0x38, 0x33, 0x80, 0xc0, 0x00, 0xe0, 0x03, 0x80, 0x38, 0x33, 0x80, 0xe0, 0x00, 
	0xe7, 0xf3, 0x83, 0x38, 0x0f, 0xff, 0xfe, 0x30, 0xe7, 0xf3, 0x83, 0x38, 0x0f, 0xff, 0xfe, 0x30, 
	0xe7, 0xf3, 0x83, 0xef, 0x84, 0x3f, 0xbf, 0xf0, 0xe7, 0xf3, 0x83, 0xc7, 0xc0, 0x1f, 0x39, 0xf0, 
	0xe7, 0xf3, 0x83, 0xc7, 0xc0, 0x1f, 0x39, 0xf0, 0xe7, 0xf3, 0x83, 0xc1, 0xf0, 0x60, 0x06, 0x70, 
	0xe7, 0xf3, 0x83, 0xc1, 0xf0, 0x60, 0x06, 0x30, 0xe0, 0x03, 0x83, 0xc7, 0xcf, 0x9f, 0xc0, 0x30, 
	0xe0, 0x03, 0x83, 0xc7, 0xcf, 0x9f, 0xc0, 0x30, 0xff, 0xff, 0x87, 0xef, 0xcf, 0x9f, 0xf0, 0x30, 
	0xff, 0xff, 0x9f, 0x3f, 0xcf, 0x9c, 0x38, 0x30, 0xff, 0xff, 0x9f, 0x3f, 0xcf, 0x9c, 0x38, 0x30
};

// WiFi Logo
const uint8_t wifiLogo[] PROGMEM = {
  0x1F, 0x00,
  0x60, 0xC0,
  0x8E, 0x20,
  0x31, 0x80,
  0x44, 0x40,
  0x0A, 0x00,
  0x11, 0x00,
  0x04, 0x00
};

// Firmware Update
void handleUpdate() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 28);
    display.print(F("Firmware Updating..."));
    display.display();
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {  // Start OTA update
      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(0, 28);
      display.print(F("Update Failed"));
      display.display();
      return;
    }
  }
  else if (upload.status == UPLOAD_FILE_WRITE) {  // Write OTA data
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(0, 28);
      display.print(F("Update Failed"));
      display.display();
      return;
    }
  }
  else if (upload.status == UPLOAD_FILE_END) {  // Finalise update
    if (Update.end(true)) {
      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(0, 28);
      display.print(F("Firmware Updated"));
      display.display();
    } else {
      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(0, 28);
      display.print(F("Update Failed"));
      display.display();
    }
  }
}

void updateFinished() {
    server.send(200, "text/plain", Update.hasError() ? "Update Failed" : "Update Successful. Rebooting...");
    delay(1000);
    ESP.restart();
}


void setup() {
  // Power on
  pinMode(calButtonPin, INPUT_PULLUP);  // Calibration pin
  pinMode(powerPin, OUTPUT);            // Power off pin
  digitalWrite(powerPin, LOW);          // Power initilised on
  unsigned long startTime = millis();
  while (digitalRead(calButtonPin) == LOW) {
    if (millis() - startTime >= powerOnDuration) {
      break;
    }
  }
  if (digitalRead(calButtonPin) == HIGH && millis() - startTime < powerOnDuration) {
    digitalWrite(powerPin, HIGH);
    return;
  }

  // ESP32 initialisation
  setCpuFrequencyMhz(80);          // Reduce CPU frequency
  esp_bt_controller_disable();     // Turn off bluetooth
  pinMode(batteryPin, INPUT);      // Battery monitoring
  pinMode(MD62VccPin, INPUT);      // MD62 Vcc monitoring
  analogReadResolution(12);        // Internal ADC 12-bit
  analogSetAttenuation(ADC_11db);  // Internal ADC 2.5V range 
  pinMode(ADDRPin, OUTPUT);        // ADS1115 Address pin
  digitalWrite(ADDRPin, LOW);      // Set to 0x48
  Wire.begin(SDAPin, SCLPin);      // I2C start
  Wire.setClock(400000);           // I2C clock speed 400 kHz
  EEPROM.begin(64);                // EEPROM start

  // Peripheral device initialisation
  bool SGP40Init = sgp40.begin();               // SGP40 start, default 0X59
  bool DS3502Init = ds3502.begin();             // DS3502 start, default 0X28
  bool ADS1115Init = ads.begin(0x48);           // ADS1115 start, default 0X48
  bool SH1106Init = display.begin(0x3C, true);  // OLED start, default 0X3C, reset
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(2);
  display.setCursor(10, 0);
  display.print(F("System Dx"));
  display.setTextSize(1);
  display.setCursor(22, 22);
  display.print(F(" SH1106: "));
  display.print(SH1106Init ? F("OK") : F("FAIL"));
  display.setCursor(22, 32);
  display.print(F(" DS3502: "));
  display.print(DS3502Init ? F("OK") : F("FAIL"));
  display.setCursor(22, 42);
  display.print(F("ADS1115: "));
  display.print(ADS1115Init ? F("OK") : F("FAIL"));
  display.setCursor(22, 52);
  display.print(F("  SGP40: "));
  display.print(SGP40Init ? F("OK") : F("FAIL"));
  display.display();
  delay(500);

  // Starting screen
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(28, 2);
  display.print(F("Trimix"));
  display.setCursor(16, 24);
  display.print(F("Analyser"));
  display.setTextSize(1);
  display.setCursor(19, 50);
  display.print(F("initialising..."));
  display.display();
  delay(2000);

  // Load calibration values
  EEPROM.get(ADDR_HELIUM_POLARITY, heliumPolarity);
  EEPROM.get(ADDR_WIPER_VALUE, bestWiperValue);
  EEPROM.get(ADDR_OXYGEN_CAL_PERCENTAGE, OxygenCalPercentage);
  EEPROM.get(ADDR_HELIUM_CAL_PERCENTAGE, HeliumCalPercentage);
  EEPROM.get(ADDR_OXYGEN_CAL_VOLTAGE, oxygencalVoltage);
  EEPROM.get(ADDR_PURE_OXYGEN_VOLTAGE, pureoxygenVoltage);
  EEPROM.get(ADDR_HELIUM_CAL_VOLTAGE, heliumcalVoltage);
  EEPROM.get(ADDR_WIFI_STATUS, wifiEnabled);

  if (isnan(heliumPolarity)) {
    heliumPolarity = false;
  }
  if (isnan(bestWiperValue) || bestWiperValue <= 0.0) {
    bestWiperValue = defaultwiperValue;
  }
  if (isnan(OxygenCalPercentage) || OxygenCalPercentage <= 0.0) {
    OxygenCalPercentage = defaultOxygenCalPercentage;
  }
  if (isnan(HeliumCalPercentage) || HeliumCalPercentage <= 0.0) {
    HeliumCalPercentage = defaultHeliumCalPercentage;
  }
  if (isnan(oxygencalVoltage) || oxygencalVoltage <= 0.0) {
    oxygencalVoltage = defaultOxygenCalVoltage;
  }
  if (isnan(pureoxygenVoltage) || pureoxygenVoltage <= 0.0) {
    pureoxygenVoltage = defaultPureOxygenVoltage;
  }
  if (isnan(heliumcalVoltage) || heliumcalVoltage <= 0.0) {
    heliumcalVoltage = defaultHeliumCalVoltage;
  }
  if (isnan(wifiEnabled)) {
    wifiEnabled = false;
  }
  ds3502.setWiper(bestWiperValue);
  if (pureoxygenVoltage > oxygencalVoltage) {
    isTwoPointCalibrated = true;
  } else {
    isTwoPointCalibrated = false;
  }

  // Wifi
  if (wifiEnabled) {
    WiFi.softAP(ssid, password);
    IPAddress IP = WiFi.softAPIP();
    esp_wifi_set_max_tx_power(8);
    server.on("/", []() {
      server.send(200, "text/html", htmlPage);
    });
    server.on("/manual_calibration", []() {
      server.send(200, "text/html", manualCalibrationPage);
    });
    server.on("/data", handleData);
    server.on("/save_calibration", handleManualCalibration);
    server.on("/update", HTTP_POST, updateFinished, handleUpdate);
    server.begin();
    server.handleClient();

    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(22, 0);
    display.print(F("WiFi On"));
    display.setTextSize(1);
    display.setCursor(0, 26);
    display.print(F("SSID: trimix_analyser"));
    display.setCursor(0, 40);
    display.print(F("Password: 12345678"));
    display.setCursor(0, 54);
    display.print(F("URL: "));
    display.print(IP);
    display.display();
    delay(1000);

    display.clearDisplay();
    display.drawBitmap(
      34,
      1,
      qrCodeBitmap,
      60,
      60,
      SH110X_WHITE
    );
    display.display();
    delay(1000);
  } else {
    esp_wifi_stop();         // Turn off wifi
    setCpuFrequencyMhz(20);  // Further reduce CPU frequency
  }

  // Display calibration values
  avgMD62Voltage = analogReadMilliVolts(MD62VccPin) / 1000.0 * 2.0;     // Initial MD62 Vcc voltage
  avgBatteryVoltage = analogReadMilliVolts(batteryPin) / 1000.0 * 2.05;  // Initial battery voltage
  calibrationDisplay();
}


void loop() {
  unsigned long currentTime = millis();
  
  // HTML update
  if (wifiEnabled) {
    server.handleClient();
  }

  // Button state
  checkCalibrationButton();
  if (isShortPress) {
    isShortPress = false;
    enterCalibrationMode();  // Short press to enter calibration mode
  } else if (isPowerOff) {
    isPowerOff = false;
    powerOffMode();          // Hold to power off
  }
  
  // Auto shut down
  if (currentTime - lastButtonPressTime >= timeOutDuration) {
    for (uint8_t countDown = 5; countDown > 0; countDown--) {
      unsigned long countdownStart = millis();
      while (millis() - countdownStart < 1000) {
        checkCalibrationButton();
        if (isButtonPressed) {
          lastButtonPressTime = millis();
          return;
        }
      }
      display.clearDisplay();
      display.setTextSize(2);
      display.setCursor(10, 12);
      display.print(F("Power Off"));
      display.setCursor(58, 36);
      display.print(countDown);
      display.display();
    }
    digitalWrite(powerPin, HIGH);
  }

  // Low battery protection at 3.2 V
  if (currentTime >= 5000 && avgBatteryVoltage < 3.2) {
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(16, 12);
    display.print(F("Batt Low"));
    display.setCursor(10, 36);
    display.print(F("Power Off"));
    display.display();
    delay(500);
    digitalWrite(powerPin, HIGH);
  }  

  // Sensor sampling
  if (currentTime - lastSampleTime >= (1000 / samplingRateHz)) {
    lastSampleTime = currentTime;
    if (sampleCount == 0) {
      oxygenSum = heliumSum = MD62Sum = batterySum = 0.0;
    }
    oxygenVoltage = getOxygenVoltage();
    oxygenSum += oxygenVoltage;
    heliumVoltage = getHeliumVoltage();
    heliumSum += heliumVoltage;
    MD62Voltage = analogReadMilliVolts(MD62VccPin) / 1000.0 * 2.0;     // MD62 Vcc voltage, 1:1 divider
    MD62Sum += MD62Voltage;
    batteryVoltage = analogReadMilliVolts(batteryPin) / 1000.0 * 2.05;  // Battery voltage, 1:1 divider with offset
    batterySum += batteryVoltage;
    sampleCount++;
  }

  // Update cycle
  if (!isButtonPressed && currentTime - lastDisplayUpdate >= (1000 / displayRateHz)) {
    lastDisplayUpdate = currentTime;

    // Calculate average voltages
    avgOxygenVoltage = (sampleCount > 0) ? (oxygenSum / sampleCount) : 0.0;
    if (avgOxygenVoltage > 99.9) {
      avgOxygenVoltage = 99.9;   // Maximum oxygen voltage allowed 99.9 mV
    }
    avgHeliumVoltage = (sampleCount > 0) ? (heliumSum / sampleCount) : 0.0;
    avgMD62Voltage = (sampleCount > 0) ? (MD62Sum / sampleCount) : 0.0;
    if (avgMD62Voltage > 9.99) {
      avgMD62Voltage = 9.99;     // Maximum MD62 Vcc voltage allowed 9.99 V
    }
    avgBatteryVoltage = (sampleCount > 0) ? (batterySum / sampleCount) : 0.0;
    if (avgBatteryVoltage > 9.99) {
      avgBatteryVoltage = 9.99;  // Maximum battery voltage allowed 9.99 V
    }
    avgSampleCount = sampleCount;
    sampleCount = 0;

    // Calculate oxygen percentage
    oxygenPercentage = getOxygenPercentage();
    bool oxygenWarning = false;
    if (oxygenPercentage < 0.0) {
      oxygenPercentage = 0.0;  // Minimum oxygen percentage allowed 0%
    } else if (oxygenPercentage > 99.9) {
    oxygenPercentage = 99.9;   // Maximum oxygen percentage allowed 99.9%
    oxygenWarning = true;      // Warning if oxygen percentage exceed 99.9%
    }
    if (oxygenPercentage >= 69.0 && oxygenPercentage < 69.5) {
      if (!sixnine) {
        sixnine = true;
        sixnineStartTime = currentTime;
      } else if (currentTime - sixnineStartTime >= 2000) {
        display.clearDisplay();
        display.setTextSize(3);
        display.setCursor(28, 20);
        display.print(F("Nice"));
        display.display();
        delay(1000);
        sixnine = false;
      }
    } else {
      sixnine = false;
    }

    // Calculate helium percentage
    correctedHeliumVoltage = avgHeliumVoltage - (17.0 / (1 + exp(-0.105 * (oxygenPercentage - 52.095))));  // Helium correction factor based on oxygen percentage
    if (correctedHeliumVoltage > 999.9) {
      correctedHeliumVoltage = 999.9;   // Maximum helium voltage allowed 999.9 mV
    } else if (correctedHeliumVoltage < -999.0) {
      correctedHeliumVoltage = -999.0;  // Minimum helium voltage allowed -999.0 mV if helium sensor polarity is inverted
    }    
    heliumPercentage = (correctedHeliumVoltage > 0.0) ? (correctedHeliumVoltage / heliumcalVoltage) * HeliumCalPercentage : 0.0;
    bool heliumWarning = false;
    if (heliumPercentage < 2.0) {
      heliumPercentage = 0.0;  // Minimum helium percentage allowed 0%, treat <2% as 0%
    } else if (heliumPercentage > 99.9) {
    heliumPercentage = 99.9;   // Maximum helium percentage allowed 99.9%
    heliumWarning = true;      // Warning if helium percentage exceed 99.9%
    }

    // Calculate MOD
    mod14 = (oxygenPercentage > 0) ? (int)((1400.0 / oxygenPercentage) - 10.0) : 0;  // MOD at ppO2 1.4
    if (mod14 > 999) {
      mod14 = 999;  // Maximum MOD allowed 999 m
    }
    mod16 = (oxygenPercentage > 0) ? (int)((1600.0 / oxygenPercentage) - 10.0) : 0;  // MOD at ppO2 1.6
    if (mod16 > 999) {
      mod16 = 999;  // Maximum MOD allowed 999 m
    }

    // Calculate END
    end = (mod14 + 10.0) * (1 - heliumPercentage / 100.0) - 10.0;  // END at MOD 1.4
    if (end < 0) {
      end = 0;  // Minimum END allowed 0 m
    }

    // Calculate Density
    den = (oxygenPercentage * 0.1756 - heliumPercentage * 1.0582 + 123.46) * (mod14 + 10) / 1000;  // Density at MOD 1.4
    if (den > 99.9) {
      den = 99.9;  // Maximum density allowed 99.9 g/L
    }

    // Calculate battery percentage
    batteryPercentage = round(123 - (123 / pow((1 + pow((avgBatteryVoltage / 3.61), 80)), 0.165)));  // Battery percentage
    if (batteryPercentage > 100) {
      batteryPercentage = 100;  // Maximum battery percentage allowed 100%
    }

    // Get Time
    String elapsedTime = formatTime();

    // Get Gas Quality, 0% humidity, 20 degree Celsius
    voc = sgp40.getVOCindex(0, 20);    // VOC index
    if (voc < 1) {
      voc = 1;                         // VOC index minimum 1
    } else if (voc > 500) {
      voc = 500;                       // VOC index maximum 500
    }
    String vocStr = "(" + String(voc) + ")";
    uint8_t vocStrX = (128 - vocStr.length() * 6) / 2;
    sgp40.measureRaw(&sgpRaw, 0, 20);  // Raw reading
    if (sgpRaw < 20001) {
      sgpRaw = 20001;                  // Raw reading minimum 20001
    } else if (sgpRaw > 52767) {
      sgpRaw = 52767;                  // Raw reading maximum 52767
    }
    sgpRawCorr = sgpRaw - 20000;       // Corrected range 1-32767

    // Display
    display.clearDisplay();

    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print(elapsedTime);

    if (currentTime >= 60000) {
      display.setCursor(vocStrX, 0);
      display.print(vocStr);
    } else {
      display.setCursor(55, 0);
      display.print(F("(-)"));
    }

    if (wifiEnabled) {
      display.drawBitmap(90, 0, wifiLogo, 16, 8, SH110X_WHITE);
    }

    display.drawRect(106, 0, 20, 8, SH110X_WHITE); // Main battery rectangle
    display.fillRect(126, 3, 2, 2, SH110X_WHITE); // Battery tip
    int16_t fillWidth = map(batteryPercentage, 0, 100, 0, 16); // Percentage to fill
    display.fillRect(108, 2, fillWidth, 4, SH110X_WHITE);  // Fill battery indicator

    display.setTextSize(2);
    display.setCursor(0, 12);
    if (oxygenPercentage < 9.95) {
      display.print(F(" "));
    }
    display.print(oxygenPercentage, 1);
    if (oxygenWarning) {
      display.print(F("!"));
    }
    display.setCursor(58, 12);
    display.print(F("/"));
    if (heliumWarning) {
      display.print(F("!"));
    }
    display.setCursor(80, 12);
    if (heliumPercentage < 9.95) {
      display.print(F(" "));
    }
    display.print(heliumPercentage, 1);

    display.setTextSize(1);
    display.setCursor(0, 34);
    display.print(F("O2:"));  
    display.print(avgOxygenVoltage, 1);
    display.print(F("mV"));

    display.setCursor(68, 34);
    display.print(F("He:"));
    if (correctedHeliumVoltage <= -99.95) {
      display.print(correctedHeliumVoltage, 0);
    } else {
      display.print(correctedHeliumVoltage, 1);
    }
    display.print(F("mV"));

    display.setCursor(0, 46);
    display.print(F("MOD:"));
    display.print(mod14);
    display.print(F("/"));
    display.print(mod16);
    display.print(F("m"));

    display.setCursor(0, 56);
    display.print(F("END:"));
    display.print(end);
    display.print(F("m"));

    display.setCursor(80, 46);
    display.print(F("Density:"));
    display.setCursor(86, 56);
    display.print(den, 1);
    display.print(F("g/L"));
    display.display();
  }
}