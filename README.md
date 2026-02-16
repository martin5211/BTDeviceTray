# BT Device Tray

A lightweight Windows system tray utility for managing Bluetooth device connections and monitoring battery levels. Quickly connect, disconnect, and check the status of your paired Bluetooth devices without opening Windows Settings.

## Features

- **System tray integration** — Lives in the notification area for quick, always-available access
- **Device management** — Connect and disconnect paired Bluetooth Classic and BLE devices with a single click
- **Battery monitoring** — Displays battery levels for connected devices that support it
- **Run on startup** — Optional auto-launch at login
- **Dark mode support** — Follows the Windows system theme
- **Lightweight** — Native C++ application with minimal resource usage

## How to Use

1. **Install** from the [Microsoft Store](https://apps.microsoft.com/detail/BTDeviceTray) or build from source
2. The app starts in the **system tray** (notification area near the clock)
3. **Left-click or right-click** the tray icon to open the device menu
4. Your **paired Bluetooth devices** are listed with their connection status and battery level
5. **Click a device** to toggle its connection
6. Enable **Run on startup** from the menu to launch automatically at login

## Requirements

- Windows 10 (version 1809) or later

## Build from Source

1. Install [Visual Studio 2022](https://visualstudio.microsoft.com/) with the **Desktop development with C++** and **Windows App SDK** workloads
2. Clone the repository and open `BTDeviceTray.sln`
3. Build and run in Release/x64

## Documentation

- [Privacy Policy](PRIVACY.md)
- [License (MIT)](LICENSE)
