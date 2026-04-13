# GroWise-Production-Firmware-ESP32-Hydroponics-Controller-
Production ESP32 firmware for the GroWise Hydro console: live monitoring, Wi‑Fi provisioning, web calibration tools, and daily CSV logging to LittleFS for easy field operations, analytics, and AI model training.

This repository contains production-grade ESP32 firmware for a hydroponics monitoring and control node.  
It provides live sensor monitoring, Wi‑Fi provisioning, calibration tools, and automatic daily CSV logging for downstream analytics and AI workflows.[file:1]

---

## Features

- **Live sensor dashboard**
  - Real‑time view of key hydroponic parameters (pH, TDS/EC, temperature, etc.) via a responsive web interface.
  - Status indicators to quickly see if sensors are connected, calibrated, and within expected ranges.

- **Wi‑Fi provisioning (no hardcoded credentials)**
  - Initial access‑point (AP) mode to configure Wi‑Fi SSID and password from your phone or laptop.
  - On network failure or incorrect credentials, the node automatically returns to “GroWise setup” mode after reboot so you can recover in the field without serial access.[file:1]

- **Calibration without reflashing**
  - Web UI controls for pH and TDS/EC calibration (e.g., point selection, confirm/save).
  - Calibration data is persisted so you can re‑tune sensors after installation without touching the firmware binary.[file:1]

- **Daily CSV logging to LittleFS**
  - The firmware creates one CSV file per day in the `/logs` folder on LittleFS (e.g. `2026-04-13.csv`).[file:1]
  - Each row contains timestamped measurements suitable for later analysis, dashboards, or training local/remote AI models.

- **CSV download over HTTP**
  - Built‑in HTTP endpoint to download CSV logs directly from your browser.
  - Example download URL (replace `ip-address` with your node’s IP):
    - `http://ip-address/download?file=/logs/2026-04-13.csv`

---

## Hardware requirements

- **ESP32 board**
  - Any ESP32‑WROOM or similar dev board with sufficient flash and PSRAM recommended.
- **Sensors**
  - pH sensor with appropriate interface board.
  - TDS/EC sensor module.
  - Temperature sensor (e.g., DS18B20 or equivalent).
- **Storage**
  - On‑chip flash used via LittleFS for log files and configuration.
- **Power & peripherals**
  - Stable 5 V/12 V supply with regulator for ESP32.
  - Optional relays or MOSFET drivers for pumps, valves, or dosing systems.

> Adjust this section to match the exact sensors and breakout boards you are using.

---

## Firmware overview

The firmware is structured around these core responsibilities:[file:1]

1. **Boot and initialization**
   - Initialize serial, GPIOs, sensors, LittleFS, and configuration storage.
   - Check if valid Wi‑Fi credentials exist; if not, start in provisioning AP mode.

2. **Wi‑Fi provisioning and connectivity**
   - AP mode hosts a configuration page where you enter Wi‑Fi SSID and password.
   - Credentials are saved to persistent storage and used on subsequent boots.
   - If the device fails to connect to the configured network, it falls back to setup mode without requiring a reflash.[file:1]

3. **Web server and UI**
   - Hosts a lightweight web interface on the ESP32 IP address.
   - Exposes endpoints for:
     - Live sensor readings (e.g., JSON/REST),
     - Calibration operations,
     - CSV log download.

4. **Measurement and logging loop**
   - Periodically reads all sensors and validates the data.
   - Appends readings to the current day’s CSV file under `/logs/YYYY-MM-DD.csv`.
   - At midnight (or when the date changes), a new CSV file is started automatically.

5. **Calibration logic**
   - Provides routines to apply calibration offsets/slopes to pH and TDS/EC readings.
   - Calibration parameters are stored in non‑volatile memory so they survive reboots.[file:1]

---

## Getting started

### 1. Clone this repository

```bash
git clone https://github.com/<your-org>/<your-repo-name>.git
cd <your-repo-name>
```

### 2. Open in Arduino IDE / PlatformIO

- **Arduino IDE**
  - Install the **ESP32 Arduino** core.
  - Open `complete_esp32_code.ino` from this repository.
  - Select your ESP32 board (e.g., “ESP32 Dev Module”) and the correct COM port.

- **PlatformIO (VS Code)**
  - Create a new `platformio.ini` or import this project as an existing folder.
  - Configure the environment for your ESP32 board type.
  - Build and upload via the usual `pio run -t upload` workflow.

### 3. Configure libraries

Ensure the following libraries (or their equivalents) are installed:

- ESP32 core for Arduino.
- LittleFS (or ESP32 LittleFS adapter library).
- Wi‑Fi & WebServer libraries (standard ESP32/Arduino networking stack).
- Sensor‑specific libraries for your pH, TDS/EC, and temperature modules.

> Match the exact library list with your `#include` statements in `complete_esp32_code.ino`.

---

## First boot and Wi‑Fi provisioning

1. **Flash the firmware**
   - Connect the ESP32 via USB and upload the sketch from Arduino IDE or PlatformIO.

2. **Connect to the setup AP**
   - On first boot, the ESP32 starts in AP mode (e.g., SSID like `GroWise-Setup`).
   - Connect your phone or laptop to this AP.

3. **Open the provisioning page**
   - In your browser, navigate to the default AP IP (commonly `192.168.4.1` or as defined in your code).
   - Enter your Wi‑Fi SSID and password and click “Save”.

4. **Automatic restart**
   - The device reboots and attempts to connect to the configured Wi‑Fi network.
   - If it cannot connect, it will fall back to setup mode on the next restart so you can reconfigure credentials.[file:1]

---

## Using the web dashboard

1. **Find the ESP32 IP address**
   - Check your router’s DHCP client list, or
   - Use a serial monitor to read the IP printed on boot.

2. **Open the dashboard**
   - In your browser, open: `http://<ip-address>/`
   - You should see:
     - Live sensor values,
     - Status indicators,
     - Calibration controls,
     - Links or buttons for log download (depending on your UI layout).

3. **Calibrate sensors**
   - Navigate to the calibration section.
   - Follow your pH and TDS/EC calibration procedure (e.g., place probe in buffer solution, click “Calibrate”, wait for stabilization).
   - Save calibration to persist it across reboots.

---

## Downloading CSV log files

The firmware writes one CSV file per day under `/logs` in LittleFS (for example, `2026-04-13.csv`).[file:1]

To download a CSV log:

1. Identify the device IP address (for example: `192.168.1.50`).
2. Choose the log file you want to download (for example: `/logs/2026-04-13.csv`).
3. Open this URL in a browser:

```text
http://ip-address/download?file=/logs/2026-04-13.csv
```

Replace `ip-address` with your actual ESP32 IP (e.g., `http://192.168.1.50/download?file=/logs/2026-04-13.csv`).

Your browser will download the CSV file, which you can open directly in Excel, LibreOffice, or import into Python, R, MATLAB, or any analytics pipeline.

---

## Example screenshots

You can place screenshots in a `docs/` folder and reference them here:

- Main dashboard view:

  ![ESP32 Hydroponics Dashboard](docs/dashboard.png)

- Calibration page:

  ![Calibration Interface](docs/calibration.png)

- Log files listing (if you expose one):

  ![CSV Logs Listing](docs/logs.png)

> Replace the file names and paths with your actual screenshots.

---

## Folder structure (recommended)

```text
.
├── complete_esp32_code.ino   # Main firmware sketch
├── data/                     # (Optional) Static web assets for SPIFFS/LittleFS upload
├── docs/                     # Screenshots and documentation images
└── README.md                 # This documentation
```

Adjust this layout to match your build system (Arduino IDE vs PlatformIO) and how you package web assets.

---

## License

Add your preferred license here, for example:

```text
Copyright (c) 2026 GroWise Pvt. Ltd.

This project is licensed under the MIT License - see the LICENSE file for details.
```

Or replace with your internal / proprietary notice if this is not open‑source.# ESP32 Hydroponics Controller – GroWise Production Firmware

This repository contains production-grade ESP32 firmware for a hydroponics monitoring and control node.  
It provides live sensor monitoring, Wi‑Fi provisioning, calibration tools, and automatic daily CSV logging for downstream analytics and AI workflows.[file:1]

---

## Features

- **Live sensor dashboard**
  - Real‑time view of key hydroponic parameters (pH, TDS/EC, temperature, etc.) via a responsive web interface.
  - Status indicators to quickly see if sensors are connected, calibrated, and within expected ranges.

- **Wi‑Fi provisioning (no hardcoded credentials)**
  - Initial access‑point (AP) mode to configure Wi‑Fi SSID and password from your phone or laptop.
  - On network failure or incorrect credentials, the node automatically returns to “GroWise setup” mode after reboot so you can recover in the field without serial access.[file:1]

- **Calibration without reflashing**
  - Web UI controls for pH and TDS/EC calibration (e.g., point selection, confirm/save).
  - Calibration data is persisted so you can re‑tune sensors after installation without touching the firmware binary.[file:1]

- **Daily CSV logging to LittleFS**
  - The firmware creates one CSV file per day in the `/logs` folder on LittleFS (e.g. `2026-04-13.csv`).[file:1]
  - Each row contains timestamped measurements suitable for later analysis, dashboards, or training local/remote AI models.

- **CSV download over HTTP**
  - Built‑in HTTP endpoint to download CSV logs directly from your browser.
  - Example download URL (replace `ip-address` with your node’s IP):
    - `http://ip-address/download?file=/logs/2026-04-13.csv`

---

## Hardware requirements

- **ESP32 board**
  - Any ESP32‑WROOM or similar dev board with sufficient flash and PSRAM recommended.
- **Sensors**
  - pH sensor with appropriate interface board.
  - TDS/EC sensor module.
  - Temperature sensor (e.g., DS18B20 or equivalent).
- **Storage**
  - On‑chip flash used via LittleFS for log files and configuration.
- **Power & peripherals**
  - Stable 5 V/12 V supply with regulator for ESP32.
  - Optional relays or MOSFET drivers for pumps, valves, or dosing systems.

> Adjust this section to match the exact sensors and breakout boards you are using.

---

## Firmware overview

The firmware is structured around these core responsibilities:[file:1]

1. **Boot and initialization**
   - Initialize serial, GPIOs, sensors, LittleFS, and configuration storage.
   - Check if valid Wi‑Fi credentials exist; if not, start in provisioning AP mode.

2. **Wi‑Fi provisioning and connectivity**
   - AP mode hosts a configuration page where you enter Wi‑Fi SSID and password.
   - Credentials are saved to persistent storage and used on subsequent boots.
   - If the device fails to connect to the configured network, it falls back to setup mode without requiring a reflash.[file:1]

3. **Web server and UI**
   - Hosts a lightweight web interface on the ESP32 IP address.
   - Exposes endpoints for:
     - Live sensor readings (e.g., JSON/REST),
     - Calibration operations,
     - CSV log download.

4. **Measurement and logging loop**
   - Periodically reads all sensors and validates the data.
   - Appends readings to the current day’s CSV file under `/logs/YYYY-MM-DD.csv`.
   - At midnight (or when the date changes), a new CSV file is started automatically.

5. **Calibration logic**
   - Provides routines to apply calibration offsets/slopes to pH and TDS/EC readings.
   - Calibration parameters are stored in non‑volatile memory so they survive reboots.[file:1]

---

## Getting started

### 1. Clone this repository

```bash
git clone https://github.com/<your-org>/<your-repo-name>.git
cd <your-repo-name>
```

### 2. Open in Arduino IDE / PlatformIO

- **Arduino IDE**
  - Install the **ESP32 Arduino** core.
  - Open `complete_esp32_code.ino` from this repository.
  - Select your ESP32 board (e.g., “ESP32 Dev Module”) and the correct COM port.

- **PlatformIO (VS Code)**
  - Create a new `platformio.ini` or import this project as an existing folder.
  - Configure the environment for your ESP32 board type.
  - Build and upload via the usual `pio run -t upload` workflow.

### 3. Configure libraries

Ensure the following libraries (or their equivalents) are installed:

- ESP32 core for Arduino.
- LittleFS (or ESP32 LittleFS adapter library).
- Wi‑Fi & WebServer libraries (standard ESP32/Arduino networking stack).
- Sensor‑specific libraries for your pH, TDS/EC, and temperature modules.

> Match the exact library list with your `#include` statements in `complete_esp32_code.ino`.

---

## First boot and Wi‑Fi provisioning

1. **Flash the firmware**
   - Connect the ESP32 via USB and upload the sketch from Arduino IDE or PlatformIO.

2. **Connect to the setup AP**
   - On first boot, the ESP32 starts in AP mode (e.g., SSID like `GroWise-Setup`).
   - Connect your phone or laptop to this AP.

3. **Open the provisioning page**
   - In your browser, navigate to the default AP IP (commonly `192.168.4.1` or as defined in your code).
   - Enter your Wi‑Fi SSID and password and click “Save”.

4. **Automatic restart**
   - The device reboots and attempts to connect to the configured Wi‑Fi network.
   - If it cannot connect, it will fall back to setup mode on the next restart so you can reconfigure credentials.[file:1]

---

## Using the web dashboard

1. **Find the ESP32 IP address**
   - Check your router’s DHCP client list, or
   - Use a serial monitor to read the IP printed on boot.

2. **Open the dashboard**
   - In your browser, open: `http://<ip-address>/`
   - You should see:
     - Live sensor values,
     - Status indicators,
     - Calibration controls,
     - Links or buttons for log download (depending on your UI layout).

3. **Calibrate sensors**
   - Navigate to the calibration section.
   - Follow your pH and TDS/EC calibration procedure (e.g., place probe in buffer solution, click “Calibrate”, wait for stabilization).
   - Save calibration to persist it across reboots.

---

## Downloading CSV log files

The firmware writes one CSV file per day under `/logs` in LittleFS (for example, `2026-04-13.csv`).[file:1]

To download a CSV log:

1. Identify the device IP address (for example: `192.168.1.50`).
2. Choose the log file you want to download (for example: `/logs/2026-04-13.csv`).
3. Open this URL in a browser:

```text
http://ip-address/download?file=/logs/2026-04-13.csv
```

Replace `ip-address` with your actual ESP32 IP (e.g., `http://192.168.1.50/download?file=/logs/2026-04-13.csv`).

Your browser will download the CSV file, which you can open directly in Excel, LibreOffice, or import into Python, R, MATLAB, or any analytics pipeline.

---

## Example screenshots

You can place screenshots in a `docs/` folder and reference them here:

- Main dashboard view:

  ![ESP32 Hydroponics Dashboard](docs/dashboard.png)

- Calibration page:

  ![Calibration Interface](docs/calibration.png)

- Log files listing (if you expose one):

  ![CSV Logs Listing](docs/logs.png)

> Replace the file names and paths with your actual screenshots.

---

## Folder structure (recommended)

```text
.
├── complete_esp32_code.ino   # Main firmware sketch
├── data/                     # (Optional) Static web assets for SPIFFS/LittleFS upload
├── docs/                     # Screenshots and documentation images
└── README.md                 # This documentation
```

Adjust this layout to match your build system (Arduino IDE vs PlatformIO) and how you package web assets.

---

## License

Add your preferred license here, for example:

```text
Copyright (c) 2026 GroWise Pvt. Ltd.

This project is licensed under the MIT License - see the LICENSE file for details.
```

Or replace with your internal / proprietary notice if this is not open‑source.
