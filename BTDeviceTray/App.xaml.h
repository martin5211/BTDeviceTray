#pragma once

#include "App.g.h"
#include "TrayIcon.h"
#include "BluetoothDeviceManager.h"
#include "BatteryMonitor.h"
#include <memory>

namespace winrt::BTDeviceTray::implementation
{
    struct App : AppT<App>
    {
        App();
        ~App();

        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);

    private:
        void OnDeviceListChanged();
        void OnDeviceClicked(const std::wstring& deviceId);
        void OnBatteryUpdated(const std::wstring& deviceId, uint8_t level);
        void Shutdown();

        winrt::BTDeviceTray::MainWindow m_window{ nullptr };
        std::unique_ptr<TrayIcon> m_trayIcon;
        std::unique_ptr<BluetoothDeviceManager> m_btManager;
        std::unique_ptr<BatteryMonitor> m_batteryMonitor;
    };
}

namespace winrt::BTDeviceTray::factory_implementation
{
    struct App : AppT<App, implementation::App>
    {
    };
}
