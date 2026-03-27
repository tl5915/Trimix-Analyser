#include <pgmspace.h>
#pragma once

const char manualCalibrationPage[] PROGMEM = R"HTML(
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
)HTML";