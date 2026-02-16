#pragma once

#include "DeviceInfo.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <functional>

namespace winrt
{
    namespace WDE = Windows::Devices::Enumeration;
}

class BluetoothDeviceManager
{
public:
    using DeviceListChangedCallback = std::function<void()>;

    BluetoothDeviceManager();
    ~BluetoothDeviceManager();

    BluetoothDeviceManager(const BluetoothDeviceManager&) = delete;
    BluetoothDeviceManager& operator=(const BluetoothDeviceManager&) = delete;

    void Start();
    void Stop();

    void ConnectAsync(const std::wstring& deviceId);
    void DisconnectAsync(const std::wstring& deviceId);
    void ToggleConnectionAsync(const std::wstring& deviceId);

    std::vector<DeviceInfo> GetDevices() const;

    void UpdateBatteryLevel(const std::wstring& deviceId, uint8_t level);

    void SetDeviceListChangedCallback(DeviceListChangedCallback callback);

private:
    void OnClassicDeviceAdded(winrt::WDE::DeviceWatcher sender, winrt::WDE::DeviceInformation info);
    void OnClassicDeviceUpdated(winrt::WDE::DeviceWatcher sender, winrt::WDE::DeviceInformationUpdate update);
    void OnClassicDeviceRemoved(winrt::WDE::DeviceWatcher sender, winrt::WDE::DeviceInformationUpdate update);

    void OnBleDeviceAdded(winrt::WDE::DeviceWatcher sender, winrt::WDE::DeviceInformation info);
    void OnBleDeviceUpdated(winrt::WDE::DeviceWatcher sender, winrt::WDE::DeviceInformationUpdate update);
    void OnBleDeviceRemoved(winrt::WDE::DeviceWatcher sender, winrt::WDE::DeviceInformationUpdate update);

    void NotifyChanged();

    static bool GetIsConnected(const winrt::Windows::Foundation::Collections::IMapView<winrt::hstring, winrt::Windows::Foundation::IInspectable>& props);

    mutable std::mutex m_mutex;
    std::unordered_map<std::wstring, DeviceInfo> m_devices;

    // Keep device/connection references alive to maintain connections
    std::unordered_map<std::wstring, winrt::Windows::Devices::Bluetooth::BluetoothDevice> m_activeClassic;
    std::unordered_map<std::wstring, winrt::Windows::Devices::Bluetooth::BluetoothLEDevice> m_activeBle;

    winrt::WDE::DeviceWatcher m_classicWatcher{ nullptr };
    winrt::WDE::DeviceWatcher m_bleWatcher{ nullptr };

    winrt::event_token m_classicAdded;
    winrt::event_token m_classicUpdated;
    winrt::event_token m_classicRemoved;
    winrt::event_token m_bleAdded;
    winrt::event_token m_bleUpdated;
    winrt::event_token m_bleRemoved;

    DeviceListChangedCallback m_changedCallback;
};
