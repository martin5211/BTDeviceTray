#pragma once

#include <string>
#include <functional>
#include <atomic>
#include <vector>
#include "DeviceInfo.h"

class BluetoothDeviceManager;

class BatteryMonitor
{
public:
    using BatteryUpdatedCallback = std::function<void(const std::wstring& deviceId, uint8_t level)>;
    using DeviceSnapshotProvider = std::function<std::vector<DeviceInfo>()>;

    BatteryMonitor();
    ~BatteryMonitor();

    BatteryMonitor(const BatteryMonitor&) = delete;
    BatteryMonitor& operator=(const BatteryMonitor&) = delete;

    void Start(DeviceSnapshotProvider provider);
    void Stop();

    void SetBatteryUpdatedCallback(BatteryUpdatedCallback callback);

private:
    winrt::fire_and_forget PollLoop();

    std::atomic<bool> m_running{ false };
    DeviceSnapshotProvider m_deviceProvider;
    BatteryUpdatedCallback m_batteryCallback;
};
