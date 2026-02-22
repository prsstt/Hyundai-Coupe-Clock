# Custom OBD2 OLED Dashboard (XIAO RP2040) 🚗💨

A high-performance, customizable OLED dashboard and trip computer for your car. Powered by the **Seeed Studio XIAO RP2040**, this project connects to your car's ECU via an ELM327 OBD2 Bluetooth adapter to display real-time telemetry, smart gear suggestions, instant fuel consumption, and smooth 1-bit animations.

![3a78ca27-afe5-48ef-bc8c-3d36977cd5f3](https://github.com/user-attachments/assets/ba7b2a24-e750-484b-82ea-529d0cb5f5a8)
![887cd838-ca5f-4a69-b703-652de8fc7917](https://github.com/user-attachments/assets/ff09f679-9ae9-4874-a387-8dba8fc6aeec)

## ✨ Features

* **Custom Startup:** Displays a Hyundai logo (customizable) upon boot.
* **Two UI Modes:** Choose between a classic **List Menu** or a beautiful, rotating **3D Animated Cube Menu** to navigate between applications.
* **Persistent Settings:** Uses EEPROM (Flash emulation) to save your preferred Start Screen and Menu Style across power cycles.
* **Bocchi The Rock! Animation:** A dedicated screen playing a smooth, full-screen 1-bit animation directly from PROGMEM.
* **Smart Dashboard:** Displays real-time speed (km/h) and calculates the current gear based on RPM/Speed ratios (Currently calibrated for Hyundai Coupe/Tiburon). Features an Eco-Shift Assistant suggesting upshifts/downshifts.
* **Instant Fuel Consumption:** Calculates real-time L/100km (while driving) and L/h (while idling) using OBD2 MAF data, featuring an Exponential Moving Average (EMA) algorithm for smooth, factory-like gauge updates.

## 🛠️ Hardware Requirements

1. **Seeed Studio XIAO RP2040** (Chosen for its large RAM which easily handles 3D animations and full-buffer OLED graphics).
2. **0.96" or 1.3" OLED Display** (SSD1306, I2C, 128x64).
3. **HC-05 Bluetooth Module** (Working on 3.3V logic).
4. **ELM327 Bluetooth OBD2 Adapter** (Plugged into your car).
5. **Momentary Push Button** (or hidden tactile switch) for menu navigation.

## 🔌 Wiring & Pinout

The XIAO RP2040 uses 3.3V logic, which perfectly matches the OLED and HC-05 data lines. No level shifters required!

| Component | XIAO RP2040 Pin | Notes |
| :--- | :--- | :--- |
| OLED SCL | **D5** | I2C Clock (Hardware I2C) |
| OLED SDA | **D4** | I2C Data (Hardware I2C) |
| HC-05 TX | **D7 (RX)** | Hardware Serial1 |
| HC-05 RX | **D6 (TX)** | Hardware Serial1 |
| Button | **D2** | Other leg to GND (Uses internal pull-up) |
| Power (All) | **5V / 3.3V** | Depending on your OLED/HC-05 versions |
| GND (All) | **GND** | Common Ground |

## ⚙️ HC-05 Bluetooth Setup (Crucial)

Before running the code, your HC-05 module must be configured as a **Master** device and bound to your ELM327's MAC address. 
You can do this by putting the HC-05 into AT Command mode and sending:

1. `AT+ROLE=1` (Set as Master)
2. `AT+CMODE=0` (Connect to specific address)
3. `AT+BIND=xx,xx,xxxxxx` (Replace with your ELM327 MAC address)
4. `AT+UART=38400,0,0` (Standard ELM327 baud rate)

## 💻 Software Installation

This project is built using **PlatformIO** (VS Code).

1. Clone this repository.
2. Open the project in PlatformIO.
3. Ensure your `platformio.ini` includes the necessary libraries:

    ```ini
    [env:seeed_xiao_rp2040]
    platform = raspberrypi
    board = seeed_xiao_rp2040
    framework = arduino
    monitor_speed = 115200

    lib_deps = 
        olikraus/U8g2 @ ^2.35.9
        powerbroker2/ELMduino @ ^3.2.5
    ```

4. Build and Upload to your XIAO RP2040.

## 🕹️ How to Use

The entire UI is controlled via a single button:
* **Short Press:** Scroll through the menu, change options, or switch animation sub-frames.
* **Long Press (>200ms):** Enter a selected application, confirm a setting, or exit back to the main menu.

## 🔧 Customization (For your car)

The gear ratios are currently configured for a **Hyundai Coupe 2.0 (Tiburon)**. To calibrate it for your car, open `main.cpp`, find the `drawDashboard()` function, and modify the RPM/Speed ratio thresholds:

```cpp
// Example: Calculate your ratio by driving at a steady speed in a specific gear
float ratio = displayed_rpm / (float)displayed_speed;
if (ratio > 110.0) currentGear = 1;
else if (ratio > 68.0) currentGear = 2;
// ... customize values here

## 📜 Credits & Libraries

* **[U8g2](https://github.com/olikraus/u8g2)** by olikraus - For the amazing and fast graphics rendering.
* **[ELMduino](https://github.com/PowerBroker2/ELMduino)** by powerbroker2 - For handling the complex OBD2 AT commands.
* Icons generated via **[image2cpp](https://javl.github.io/image2cpp/)**.
