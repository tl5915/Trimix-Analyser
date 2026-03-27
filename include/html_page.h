#include <pgmspace.h>
#pragma once

const char htmlPage[] PROGMEM = R"HTML(
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
)HTML";