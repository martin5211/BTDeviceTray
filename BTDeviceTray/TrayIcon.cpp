#include "pch.h"
#include "TrayIcon.h"
#include "resource.h"

// Undocumented uxtheme.dll API for dark mode support
enum PreferredAppMode { PAM_Default = 0, PAM_AllowDark = 1, PAM_ForceDark = 2, PAM_ForceLight = 3 };
using SetPreferredAppModeFn = PreferredAppMode(WINAPI*)(PreferredAppMode);

static void EnableDarkModeMenus()
{
    HMODULE hUxTheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!hUxTheme)
        return;
    auto SetPreferredAppMode = reinterpret_cast<SetPreferredAppModeFn>(GetProcAddress(hUxTheme, MAKEINTRESOURCEA(135)));
    if (SetPreferredAppMode)
        SetPreferredAppMode(PAM_AllowDark);
}

static constexpr wchar_t STARTUP_REG_KEY[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static constexpr wchar_t STARTUP_REG_VALUE[] = L"BT Device Tray";

TrayIcon::TrayIcon() = default;

TrayIcon::~TrayIcon()
{
    Remove();
    if (m_hwnd)
    {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

bool TrayIcon::Initialize(HINSTANCE hInstance)
{
    m_hInstance = hInstance;

    // Enable dark mode for popup menus (follows system theme automatically)
    EnableDarkModeMenus();

    // Register for taskbar recreation (Explorer restart)
    m_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"BTDeviceTrayMessageWindow";
    RegisterClassExW(&wc);

    // Create message-only window
    m_hwnd = CreateWindowExW(
        0,
        L"BTDeviceTrayMessageWindow",
        L"BT Device Tray",
        0,
        0, 0, 0, 0,
        HWND_MESSAGE,
        nullptr,
        hInstance,
        this);

    if (!m_hwnd)
        return false;

    AddTrayIcon();
    return true;
}

void TrayIcon::AddTrayIcon()
{
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_hwnd;
    nid.uID = TRAY_ICON_ID;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIconW(m_hInstance, MAKEINTRESOURCEW(IDI_BLUETRAY));
    wcscpy_s(nid.szTip, L"BT Device Tray");

    Shell_NotifyIconW(NIM_ADD, &nid);

    // Set version 4 for modern behavior
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);

    m_iconAdded = true;
}

void TrayIcon::RemoveTrayIcon()
{
    if (!m_iconAdded)
        return;

    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_hwnd;
    nid.uID = TRAY_ICON_ID;
    Shell_NotifyIconW(NIM_DELETE, &nid);

    m_iconAdded = false;
}

void TrayIcon::Remove()
{
    RemoveTrayIcon();
}

void TrayIcon::UpdateDeviceList(const std::vector<DeviceInfo>& devices)
{
    std::lock_guard lock(m_devicesMutex);
    m_devices = devices;
}

void TrayIcon::SetDeviceClickCallback(DeviceClickCallback callback)
{
    m_deviceClickCallback = std::move(callback);
}

void TrayIcon::SetExitCallback(ExitCallback callback)
{
    m_exitCallback = std::move(callback);
}

LRESULT CALLBACK TrayIcon::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    TrayIcon* self = nullptr;

    if (msg == WM_NCCREATE)
    {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<TrayIcon*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else
    {
        self = reinterpret_cast<TrayIcon*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self)
        return self->HandleMessage(hwnd, msg, wParam, lParam);

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT TrayIcon::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == m_taskbarCreatedMsg && m_taskbarCreatedMsg != 0)
    {
        // Explorer restarted — re-add the tray icon
        m_iconAdded = false;
        AddTrayIcon();
        return 0;
    }

    switch (msg)
    {
    case WM_TRAYICON:
    {
        // NOTIFYICON_VERSION_4: LOWORD(lParam) = event, HIWORD(lParam) = icon id
        switch (LOWORD(lParam))
        {
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            ShowContextMenu();
            break;
        case WM_LBUTTONUP:
            ShowContextMenu();
            break;
        }
        return 0;
    }

    case WM_COMMAND:
    {
        UINT cmdId = LOWORD(wParam);
        if (cmdId == IDM_EXIT)
        {
            if (m_exitCallback)
                m_exitCallback();
        }
        else if (cmdId == IDM_STARTUP)
        {
            SetStartupEnabled(!IsStartupEnabled());
        }
        else if (cmdId == IDM_ABOUT)
        {
            ShowAboutDialog();
        }
        else if (cmdId >= IDM_DEVICE_FIRST && cmdId <= IDM_DEVICE_LAST)
        {
            UINT index = cmdId - IDM_DEVICE_FIRST;
            std::wstring deviceId;
            {
                std::lock_guard lock(m_devicesMutex);
                if (index < m_devices.size())
                    deviceId = m_devices[index].id;
            }
            if (!deviceId.empty() && m_deviceClickCallback)
                m_deviceClickCallback(deviceId);
        }
        return 0;
    }

    case WM_DESTROY:
        RemoveTrayIcon();
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool TrayIcon::IsStartupEnabled()
{
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, STARTUP_REG_KEY, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return false;
    DWORD type = 0;
    bool exists = (RegQueryValueExW(hKey, STARTUP_REG_VALUE, nullptr, &type, nullptr, nullptr) == ERROR_SUCCESS);
    RegCloseKey(hKey);
    return exists;
}

void TrayIcon::SetStartupEnabled(bool enable)
{
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, STARTUP_REG_KEY, 0, KEY_WRITE, &hKey) != ERROR_SUCCESS)
        return;

    if (enable)
    {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        // Quote the path in case it contains spaces
        std::wstring value = L"\"" + std::wstring(exePath) + L"\"";
        RegSetValueExW(hKey, STARTUP_REG_VALUE, 0, REG_SZ,
            reinterpret_cast<const BYTE*>(value.c_str()),
            static_cast<DWORD>((value.length() + 1) * sizeof(wchar_t)));
    }
    else
    {
        RegDeleteValueW(hKey, STARTUP_REG_VALUE);
    }

    RegCloseKey(hKey);
}

static constexpr int IDC_ABOUT_LINK = 101;
static constexpr int IDC_ABOUT_LABEL = 102;

static bool IsDarkMode()
{
    DWORD value = 1;
    DWORD size = sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_DWORD, nullptr, &value, &size);
    return value == 0;
}

static HBRUSH s_aboutBgBrush = nullptr;
static HFONT s_aboutLinkFont = nullptr;
static bool s_aboutDark = false;

static INT_PTR CALLBACK AboutDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        s_aboutDark = IsDarkMode();
        if (s_aboutDark)
        {
            s_aboutBgBrush = CreateSolidBrush(RGB(32, 32, 32));

            // Dark title bar via DwmSetWindowAttribute(DWMWA_USE_IMMERSIVE_DARK_MODE)
            using DwmSetWinAttrFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
            HMODULE hDwm = LoadLibraryExW(L"dwmapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (hDwm)
            {
                auto fn = reinterpret_cast<DwmSetWinAttrFn>(GetProcAddress(hDwm, "DwmSetWindowAttribute"));
                if (fn)
                {
                    BOOL dark = TRUE;
                    fn(hwnd, 20, &dark, sizeof(dark));
                }
            }
        }

        // Create underlined font for the link
        HFONT hDlgFont = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
        if (!hDlgFont)
            hDlgFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        LOGFONTW lf = {};
        GetObjectW(hDlgFont, sizeof(lf), &lf);
        lf.lfUnderline = TRUE;
        s_aboutLinkFont = CreateFontIndirectW(&lf);
        if (s_aboutLinkFont)
            SendDlgItemMessageW(hwnd, IDC_ABOUT_LINK, WM_SETFONT,
                reinterpret_cast<WPARAM>(s_aboutLinkFont), TRUE);

        return TRUE;
    }

    case WM_CTLCOLORDLG:
        if (s_aboutDark && s_aboutBgBrush)
            return reinterpret_cast<INT_PTR>(s_aboutBgBrush);
        break;

    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        int id = GetDlgCtrlID(reinterpret_cast<HWND>(lParam));

        if (id == IDC_ABOUT_LINK)
        {
            SetTextColor(hdc, s_aboutDark ? RGB(100, 160, 255) : RGB(0, 102, 204));
            SetBkMode(hdc, TRANSPARENT);
            return reinterpret_cast<INT_PTR>(s_aboutDark ? s_aboutBgBrush : GetStockObject(NULL_BRUSH));
        }

        if (s_aboutDark)
        {
            SetTextColor(hdc, RGB(255, 255, 255));
            SetBkMode(hdc, TRANSPARENT);
            return reinterpret_cast<INT_PTR>(s_aboutBgBrush);
        }
        break;
    }

    case WM_SETCURSOR:
        if (GetDlgCtrlID(reinterpret_cast<HWND>(wParam)) == IDC_ABOUT_LINK)
        {
            SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32649))); // IDC_HAND
            SetWindowLongPtrW(hwnd, DWLP_MSGRESULT, TRUE);
            return TRUE;
        }
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            if (s_aboutBgBrush) { DeleteObject(s_aboutBgBrush); s_aboutBgBrush = nullptr; }
            if (s_aboutLinkFont) { DeleteObject(s_aboutLinkFont); s_aboutLinkFont = nullptr; }
            EndDialog(hwnd, 0);
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_ABOUT_LINK && HIWORD(wParam) == STN_CLICKED)
        {
            ShellExecuteW(hwnd, L"open",
                L"https://github.com/martin5211/BTDeviceTray",
                nullptr, nullptr, SW_SHOWNORMAL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static BYTE* AlignDword(BYTE* p)
{
    return reinterpret_cast<BYTE*>((reinterpret_cast<ULONG_PTR>(p) + 3) & ~3);
}

static BYTE* WriteWideString(BYTE* p, const wchar_t* s)
{
    size_t len = (wcslen(s) + 1) * sizeof(wchar_t);
    memcpy(p, s, len);
    return p + len;
}

void TrayIcon::ShowAboutDialog()
{
    BYTE buffer[2048] = {};
    BYTE* p = buffer;

    constexpr int DLG_W = 210, DLG_H = 95;

    // DLGTEMPLATE
    auto* dlg = reinterpret_cast<DLGTEMPLATE*>(p);
    dlg->style = DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU;
    dlg->cdit = 3;
    dlg->cx = DLG_W;
    dlg->cy = DLG_H;
    p += sizeof(DLGTEMPLATE);

    // Menu: none
    *reinterpret_cast<WORD*>(p) = 0; p += sizeof(WORD);
    // Class: default
    *reinterpret_cast<WORD*>(p) = 0; p += sizeof(WORD);
    // Title
    p = WriteWideString(p, L"About");

    // --- Control 1: Static label ---
    p = AlignDword(p);
    auto* item1 = reinterpret_cast<DLGITEMTEMPLATE*>(p);
    item1->style = WS_CHILD | WS_VISIBLE | SS_CENTER;
    item1->x = 10; item1->y = 10;
    item1->cx = DLG_W - 20; item1->cy = 32;
    item1->id = IDC_ABOUT_LABEL;
    p += sizeof(DLGITEMTEMPLATE);
    *reinterpret_cast<WORD*>(p) = 0xFFFF; p += sizeof(WORD);
    *reinterpret_cast<WORD*>(p) = 0x0082; p += sizeof(WORD); // STATIC
    p = WriteWideString(p, L"BT Device Tray\n\nAuthor: Martin Caminoa");
    *reinterpret_cast<WORD*>(p) = 0; p += sizeof(WORD);

    // --- Control 2: URL link (static with SS_NOTIFY + SS_CENTER) ---
    p = AlignDword(p);
    auto* item2 = reinterpret_cast<DLGITEMTEMPLATE*>(p);
    item2->style = WS_CHILD | WS_VISIBLE | SS_CENTER | SS_NOTIFY;
    item2->x = 10; item2->y = 48;
    item2->cx = DLG_W - 20; item2->cy = 10;
    item2->id = IDC_ABOUT_LINK;
    p += sizeof(DLGITEMTEMPLATE);
    *reinterpret_cast<WORD*>(p) = 0xFFFF; p += sizeof(WORD);
    *reinterpret_cast<WORD*>(p) = 0x0082; p += sizeof(WORD); // STATIC
    p = WriteWideString(p, L"github.com/martin5211/BTDeviceTray");
    *reinterpret_cast<WORD*>(p) = 0; p += sizeof(WORD);

    // --- Control 3: OK button ---
    p = AlignDword(p);
    auto* item3 = reinterpret_cast<DLGITEMTEMPLATE*>(p);
    item3->style = WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP;
    item3->x = (DLG_W - 50) / 2; item3->y = DLG_H - 20;
    item3->cx = 50; item3->cy = 14;
    item3->id = IDOK;
    p += sizeof(DLGITEMTEMPLATE);
    *reinterpret_cast<WORD*>(p) = 0xFFFF; p += sizeof(WORD);
    *reinterpret_cast<WORD*>(p) = 0x0080; p += sizeof(WORD); // BUTTON
    p = WriteWideString(p, L"OK");
    *reinterpret_cast<WORD*>(p) = 0; p += sizeof(WORD);

    DialogBoxIndirectW(m_hInstance, reinterpret_cast<DLGTEMPLATE*>(buffer), m_hwnd, AboutDlgProc);
}

void TrayIcon::ShowContextMenu()
{
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu)
        return;

    {
        std::lock_guard lock(m_devicesMutex);

        if (m_devices.empty())
        {
            AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, L"No paired devices");
        }
        else
        {
            for (size_t i = 0; i < m_devices.size() && i <= (IDM_DEVICE_LAST - IDM_DEVICE_FIRST); ++i)
            {
                const auto& device = m_devices[i];
                UINT flags = MF_STRING;

                if (device.isConnected)
                    flags |= MF_CHECKED;

                if (device.isConnecting)
                    flags |= MF_GRAYED;

                std::wstring label = device.GetMenuLabel();
                AppendMenuW(hMenu, flags, IDM_DEVICE_FIRST + static_cast<UINT>(i), label.c_str());
            }
        }
    }

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, IsStartupEnabled() ? (MF_STRING | MF_CHECKED) : MF_STRING,
        IDM_STARTUP, L"Run on startup");
    AppendMenuW(hMenu, MF_STRING, IDM_ABOUT, L"About BT Device Tray");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"Exit");

    // Required: SetForegroundWindow before TrackPopupMenu
    SetForegroundWindow(m_hwnd);

    POINT pt;
    GetCursorPos(&pt);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hwnd, nullptr);

    // Required: post empty message to dismiss menu properly
    PostMessageW(m_hwnd, WM_NULL, 0, 0);

    DestroyMenu(hMenu);
}
