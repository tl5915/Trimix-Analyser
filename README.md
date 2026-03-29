# Trimix Analyser
Trimix analyser for scuba diving

## Material
### Housing
- Electronic junction box 158x90x60mm
- 15 mm ID, 18 mm OD PVC pipe (for sensor chamber, O2 mount to the end, He and CO on two sides, gas out on the other end)
- M16 x 1.0 tap (for O2 sensor)
- 16 mm ID silicone plug (drill hole for gas out)
- BCD nipple to 1.4 NPT male convertor (gas in)
- 3/8 UNF tap (tap on the NPT side of the above convertor to mount a o-ring-removed LP blanking plug to adjust flow rate to 2 L/min)
- 1/4 NPT female to the smallest hose barb you can find, use barb convertor if necessary (ideally 2 mm ID silicone tubing connect to sensor chamber)
### Electronics
- Oxygen sensor, Aux or Molex connector He Sensor
- Wisen MD62 Helium sensor (mount to sensor chamber with hole cut out and seal with epoxy)
- 1N5819 Schottky diode (drops the 3.3v to 3.0v that MD62 requires)
- DS3502 digital potentiometer (10k ohm), parallel connect to 510 ohm resistor to drop total resistence to 485 ohm
- 2k ohm resistors for MD62 wheastone bridge
- 47k ohm 1:1 voltage divider to monitor MD62 Vcc Gas Quality Sensor
- SGP40 gas sensor (drill hole to fit sensor into sensor chamber, seal with epoxy)
- Use JST-SH cable to connect to DS3502, provides power and I2C communication MCU and Display
- Seeed Xiao ESP32C3
- 1.3 inch I2C SH1106 OLED
- ADS1115 ADC
### Power
- 3.7V 2000 mAh LiPo battery
- TP4056 LiPo charging board
- Qi wireless charging receiver unit
- Sparkfun Soft Power Switch (connect to external momentary button, connect push and off to GPIO pins)
- Momentary button for calibration and power
- 3.3V buck-boost converter (at least 250mA output capacity)
- 100nF capacitors at converter input and output
- 47k ohm 1:1 voltage divider to monitor battery voltage

## Button logic
- Press and hold button for 1 second to power on
- In main page, single press to enter menu
- Short press to toggle, long press to select
- Press and hold for 5 seconds to power off
- Press and hold for 10 seconds will force the switch to hardware power off
- Automatic power off after 5 minutes if no button press

## WiFi
- Default off to save power, can be turned on in setting menu
- Connect WiFi hotspot and scan the QR code to see more data, maunally input calibration values, and OTA firmware upgrade
- WiFi transmission power is set to minimum, external antenna must be connected

## Usage
- Reverse helium polarity in setting menu if helium voltage is negative in trimix
- Use pure helium or standard gas trimix for helium calibration
- SGP40 reading at top middle of the display, only shows after 1 minute initialisation. This reading is not in PPM, but relative to fresh air (100), lower (towards 1) the better quality, higher (towards 500) the worse quality. Volatile organic compounds, CO, and CO2 (all gases you don't want in your cylinder) increases reading
- Connect to WiFi, scan QR code to open web page
- More sensor informations can be found on web page, also allow maunal overwrite of calibration values
- Upload .bin file for OTA firmware update