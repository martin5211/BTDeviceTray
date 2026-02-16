#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#if __has_include("App.g.cpp")
#include "App.g.cpp"
#endif

#include <microsoft.ui.xaml.window.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Microsoft.UI.Windowing.h>

// WinUI 3 C++ entry point
int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int)
{
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    ::winrt::Microsoft::UI::Xaml::Application::Start(
        [](auto&&) { ::winrt::make<::winrt::BTDeviceTray::implementation::App>(); });
    return 0;
}

namespace winrt::BTDeviceTray::implementation
{
    App::App()
    {
#if defined _DEBUG && !defined DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION
        UnhandledException([](IInspectable const&, Microsoft::UI::Xaml::UnhandledExceptionEventArgs const& e)
        {
            if (IsDebuggerPresent())
            {
                auto errorMessage = e.Message();
                __debugbreak();
            }
        });
#endif
    }

    App::~App()
    {
        Shutdown();
    }

    void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&)
    {
        // Create and immediately hide the MainWindow (WinUI 3 requires a Window)
        m_window = winrt::make<BTDeviceTray::implementation::MainWindow>();
        m_window.Activate();

        // Get the native HWND and hide the window
        HWND hwnd = nullptr;
        auto windowNative = m_window.as<IWindowNative>();
        windowNative->get_WindowHandle(&hwnd);
        ShowWindow(hwnd, SW_HIDE);

        // Hide from taskbar / Alt+Tab
        auto windowId = Microsoft::UI::GetWindowIdFromWindow(hwnd);
        auto appWindow = Microsoft::UI::Windowing::AppWindow::GetFromWindowId(windowId);
        if (auto presenter = appWindow.Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>())
        {
            presenter.IsMinimizable(false);
            presenter.IsMaximizable(false);
            presenter.IsAlwaysOnTop(false);
        }
        appWindow.IsShownInSwitchers(false);

        // Get HINSTANCE
        HINSTANCE hInstance = reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr));

        // Initialize tray icon
        m_trayIcon = std::make_unique<TrayIcon>();
        m_trayIcon->SetDeviceClickCallback([this](const std::wstring& id) { OnDeviceClicked(id); });
        m_trayIcon->SetExitCallback([this]() { Shutdown(); });
        m_trayIcon->Initialize(hInstance);

        // Initialize Bluetooth device manager
        m_btManager = std::make_unique<BluetoothDeviceManager>();
        m_btManager->SetDeviceListChangedCallback([this]() { OnDeviceListChanged(); });
        m_btManager->Start();

        // Initialize battery monitor
        m_batteryMonitor = std::make_unique<BatteryMonitor>();
        m_batteryMonitor->SetBatteryUpdatedCallback(
            [this](const std::wstring& id, uint8_t level) { OnBatteryUpdated(id, level); });
        m_batteryMonitor->Start([this]() { return m_btManager->GetDevices(); });
    }

    void App::OnDeviceListChanged()
    {
        if (m_btManager && m_trayIcon)
        {
            auto devices = m_btManager->GetDevices();
            m_trayIcon->UpdateDeviceList(devices);
        }
    }

    void App::OnDeviceClicked(const std::wstring& deviceId)
    {
        if (m_btManager)
        {
            m_btManager->ToggleConnectionAsync(deviceId);
        }
    }

    void App::OnBatteryUpdated(const std::wstring& deviceId, uint8_t level)
    {
        if (m_btManager)
        {
            m_btManager->UpdateBatteryLevel(deviceId, level);
        }
    }

    void App::Shutdown()
    {
        if (m_batteryMonitor)
        {
            m_batteryMonitor->Stop();
            m_batteryMonitor.reset();
        }

        if (m_btManager)
        {
            m_btManager->Stop();
            m_btManager.reset();
        }

        if (m_trayIcon)
        {
            m_trayIcon->Remove();
            m_trayIcon.reset();
        }

        if (m_window)
        {
            m_window.Close();
            m_window = nullptr;
        }

        // Exit the application
        Exit();
    }
}
