#pragma once
#include <string>
#include <optional>

enum class DeviceType
{
    Classic,
    BLE
};

struct DeviceInfo
{
    std::wstring id;
    std::wstring name;
    DeviceType type = DeviceType::Classic;
    bool isConnected = false;
    bool isConnecting = false;
    std::optional<uint8_t> batteryLevel;
    uint64_t bluetoothAddress = 0;

    std::wstring GetMenuLabel() const
    {
        std::wstring label = name;

        if (isConnecting)
        {
            label += L" (Connecting...)";
        }
        else if (isConnected && batteryLevel.has_value())
        {
            label += L" (" + std::to_wstring(batteryLevel.value()) + L"%)";
        }
        else if (isConnected)
        {
            label += L" (Connected)";
        }

        return label;
    }
};
