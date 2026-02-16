#include "pch.h"
#include "TrayIcon.h"
#include "BluetoothDeviceManager.h"
#include "BatteryMonitor.h"
#include <memory>

static std::unique_ptr<TrayIcon> g_trayIcon;
static std::unique_ptr<BluetoothDeviceManager> g_btManager;
static std::unique_ptr<BatteryMonitor> g_batteryMonitor;

static void OnDeviceListChanged()
{
    if (g_btManager && g_trayIcon)
    {
        auto devices = g_btManager->GetDevices();
        g_trayIcon->UpdateDeviceList(devices);
    }
}

static void OnDeviceClicked(const std::wstring& deviceId)
{
    if (g_btManager)
    {
        g_btManager->ToggleConnectionAsync(deviceId);
    }
}

static void OnBatteryUpdated(const std::wstring& deviceId, uint8_t level)
{
    if (g_btManager)
    {
        g_btManager->UpdateBatteryLevel(deviceId, level);
    }
}

static void Shutdown()
{
    if (g_batteryMonitor)
    {
        g_batteryMonitor->Stop();
        g_batteryMonitor.reset();
    }

    if (g_btManager)
    {
        g_btManager->Stop();
        g_btManager.reset();
    }

    if (g_trayIcon)
    {
        g_trayIcon->Remove();
        g_trayIcon.reset();
    }

    PostQuitMessage(0);
}

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int)
{
    winrt::init_apartment(winrt::apartment_type::single_threaded);

    // Initialize tray icon
    g_trayIcon = std::make_unique<TrayIcon>();
    g_trayIcon->SetDeviceClickCallback(OnDeviceClicked);
    g_trayIcon->SetExitCallback(Shutdown);
    if (!g_trayIcon->Initialize(hInstance))
    {
        return 1;
    }

    // Initialize Bluetooth device manager
    g_btManager = std::make_unique<BluetoothDeviceManager>();
    g_btManager->SetDeviceListChangedCallback(OnDeviceListChanged);
    g_btManager->Start();

    // Initialize battery monitor
    g_batteryMonitor = std::make_unique<BatteryMonitor>();
    g_batteryMonitor->SetBatteryUpdatedCallback(OnBatteryUpdated);
    g_batteryMonitor->Start([]() { return g_btManager->GetDevices(); });

    // Run message loop
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Cleanup (in case Shutdown wasn't called)
    Shutdown();

    return static_cast<int>(msg.wParam);
}
