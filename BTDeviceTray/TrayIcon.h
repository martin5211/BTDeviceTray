#pragma once

#include "DeviceInfo.h"
#include <vector>
#include <functional>
#include <windows.h>

class TrayIcon
{
public:
    using DeviceClickCallback = std::function<void(const std::wstring& deviceId)>;
    using ExitCallback = std::function<void()>;

    TrayIcon();
    ~TrayIcon();

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    bool Initialize(HINSTANCE hInstance);
    void Remove();

    void UpdateDeviceList(const std::vector<DeviceInfo>& devices);
    void SetDeviceClickCallback(DeviceClickCallback callback);
    void SetExitCallback(ExitCallback callback);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void ShowContextMenu();
    void ShowAboutDialog();
    void AddTrayIcon();
    void RemoveTrayIcon();

    static bool IsStartupEnabled();
    static void SetStartupEnabled(bool enable);

    HWND m_hwnd = nullptr;
    HINSTANCE m_hInstance = nullptr;
    bool m_iconAdded = false;
    UINT m_taskbarCreatedMsg = 0;

    std::vector<DeviceInfo> m_devices;
    std::mutex m_devicesMutex;

    DeviceClickCallback m_deviceClickCallback;
    ExitCallback m_exitCallback;

    static constexpr UINT WM_TRAYICON = WM_APP + 1;
    static constexpr UINT TRAY_ICON_ID = 1;
};
