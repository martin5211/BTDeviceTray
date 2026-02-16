#include "pch.h"
#include "BluetoothDeviceManager.h"

#pragma comment(lib, "bthprops.lib")

namespace WDB = winrt::Windows::Devices::Bluetooth;
namespace WDBG = winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;
namespace WDBR = winrt::Windows::Devices::Bluetooth::Rfcomm;
namespace WDE = winrt::Windows::Devices::Enumeration;
namespace WF = winrt::Windows::Foundation;

static constexpr wchar_t PROP_IS_CONNECTED[] = L"System.Devices.Aep.IsConnected";
static constexpr wchar_t PROP_DEVICE_ADDRESS[] = L"System.Devices.Aep.DeviceAddress";
// PKEY_Devices_BatteryPercent - Windows caches HFP battery here for Classic BT devices
static constexpr wchar_t PROP_BATTERY_PERCENT[] = L"{104EA319-6EE2-4701-BD47-8DDBF425BBE5} 2";

static uint64_t ParseBluetoothAddress(const winrt::hstring& addressStr)
{
    std::wstring clean;
    for (wchar_t c : addressStr)
    {
        if (c != L':' && c != L'-')
            clean += c;
    }
    if (clean.length() != 12)
        return 0;
    try { return std::stoull(clean, nullptr, 16); }
    catch (...) { return 0; }
}

// Extract MAC address from AEP device ID string as fallback
// AEP IDs often look like: "Bluetooth#Bluetooth00:11:22:33:44:55-..."
static uint64_t ExtractAddressFromAepId(const std::wstring& aepId)
{
    for (size_t i = 0; i + 16 < aepId.length(); ++i)
    {
        if (iswxdigit(aepId[i]) && iswxdigit(aepId[i + 1]) && aepId[i + 2] == L':' &&
            iswxdigit(aepId[i + 3]) && iswxdigit(aepId[i + 4]) && aepId[i + 5] == L':' &&
            iswxdigit(aepId[i + 6]) && iswxdigit(aepId[i + 7]) && aepId[i + 8] == L':' &&
            iswxdigit(aepId[i + 9]) && iswxdigit(aepId[i + 10]) && aepId[i + 11] == L':' &&
            iswxdigit(aepId[i + 12]) && iswxdigit(aepId[i + 13]) && aepId[i + 14] == L':' &&
            iswxdigit(aepId[i + 15]) && iswxdigit(aepId[i + 16]))
        {
            std::wstring macStr = aepId.substr(i, 17);
            return ParseBluetoothAddress(winrt::hstring(macStr));
        }
    }
    return 0;
}

static uint64_t GetAddressFromProperties(
    const winrt::Windows::Foundation::Collections::IMapView<winrt::hstring, WF::IInspectable>& props)
{
    if (props.HasKey(PROP_DEVICE_ADDRESS))
    {
        auto val = props.Lookup(PROP_DEVICE_ADDRESS);
        if (val)
        {
            return ParseBluetoothAddress(winrt::unbox_value<winrt::hstring>(val));
        }
    }
    return 0;
}

static void DebugLog(const wchar_t* fmt, ...)
{
    wchar_t buf[512];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, args);
    va_end(args);
    OutputDebugStringW(buf);
}

// IOCTL_BTH_DISCONNECT_DEVICE = CTL_CODE(FILE_DEVICE_BLUETOOTH, 3, METHOD_BUFFERED, FILE_ANY_ACCESS)
static constexpr DWORD IOCTL_BTH_DISCONNECT_DEVICE_VALUE = 0x0041000CUL;

// Disconnect the Bluetooth ACL link entirely to prevent auto-reconnect.
static bool DisconnectBluetoothAcl(uint64_t btAddress)
{
    BLUETOOTH_FIND_RADIO_PARAMS rfp = { sizeof(rfp) };
    HANDLE hRadio = nullptr;
    HBLUETOOTH_RADIO_FIND hFind = BluetoothFindFirstRadio(&rfp, &hRadio);
    if (!hFind)
    {
        DebugLog(L"[BTDevTray] No Bluetooth radio found for ACL disconnect\n");
        return false;
    }
    BluetoothFindRadioClose(hFind);

    ULONGLONG addr = btAddress;
    DWORD bytesReturned = 0;
    BOOL ok = DeviceIoControl(
        hRadio,
        IOCTL_BTH_DISCONNECT_DEVICE_VALUE,
        &addr, sizeof(addr),
        nullptr, 0,
        &bytesReturned,
        nullptr);

    DWORD err = ok ? 0 : GetLastError();
    CloseHandle(hRadio);

    DebugLog(L"[BTDevTray] ACL disconnect ok=%d err=%lu\n", ok, err);
    return ok != FALSE;
}

// Connect or disconnect a Bluetooth audio device via its kernel streaming filter.
// Enumerates all audio endpoints (including unplugged/disconnected BT devices),
// traverses the device topology to find the Bluetooth filter matching the given
// address, and sends KSPROPERTY_ONESHOT_RECONNECT (0) or DISCONNECT (1).
static bool ReconnectBluetoothAudio(uint64_t btAddress, bool connect)
{
    wchar_t addrHex[13];
    _snwprintf_s(addrHex, _countof(addrHex), _TRUNCATE, L"%012llX", btAddress);
    std::wstring addrUpper(addrHex);

    DebugLog(L"[BTDevTray] ReconnectBluetoothAudio addr=%s %s\n",
        addrHex, connect ? L"CONNECT" : L"DISCONNECT");

    winrt::com_ptr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), enumerator.put_void());
    if (FAILED(hr) || !enumerator)
    {
        DebugLog(L"[BTDevTray] MMDeviceEnumerator failed hr=0x%08lX\n", hr);
        return false;
    }

    bool success = false;
    EDataFlow flows[] = { eRender, eCapture };

    for (auto flow : flows)
    {
        if (success) break;

        winrt::com_ptr<IMMDeviceCollection> endpoints;
        hr = enumerator->EnumAudioEndpoints(flow, DEVICE_STATEMASK_ALL, endpoints.put());
        if (FAILED(hr) || !endpoints) continue;

        UINT count = 0;
        endpoints->GetCount(&count);

        for (UINT i = 0; i < count && !success; ++i)
        {
            winrt::com_ptr<IMMDevice> endpoint;
            endpoints->Item(i, endpoint.put());
            if (!endpoint) continue;

            winrt::com_ptr<IDeviceTopology> topology;
            hr = endpoint->Activate(__uuidof(IDeviceTopology), CLSCTX_ALL, nullptr, topology.put_void());
            if (FAILED(hr) || !topology) continue;

            UINT connCount = 0;
            topology->GetConnectorCount(&connCount);

            for (UINT j = 0; j < connCount && !success; ++j)
            {
                winrt::com_ptr<IConnector> connector;
                topology->GetConnector(j, connector.put());
                if (!connector) continue;

                winrt::com_ptr<IConnector> peer;
                if (FAILED(connector->GetConnectedTo(peer.put())) || !peer) continue;

                winrt::com_ptr<IPart> part;
                if (FAILED(peer->QueryInterface(__uuidof(IPart), part.put_void())) || !part) continue;

                winrt::com_ptr<IDeviceTopology> filterTopology;
                if (FAILED(part->GetTopologyObject(filterTopology.put())) || !filterTopology) continue;

                LPWSTR filterId = nullptr;
                if (FAILED(filterTopology->GetDeviceId(&filterId)) || !filterId) continue;

                // Check if this is a Bluetooth audio filter matching our device address
                std::wstring filterUpper(filterId);
                std::transform(filterUpper.begin(), filterUpper.end(), filterUpper.begin(), ::towupper);

                bool isBluetooth = (filterUpper.find(L"\\\\?\\BTH") != std::wstring::npos);
                bool addressMatch = (filterUpper.find(addrUpper) != std::wstring::npos);

                if (isBluetooth && addressMatch)
                {
                    DebugLog(L"[BTDevTray] Matched BT audio filter: %s\n", filterId);

                    winrt::com_ptr<IMMDevice> filterDevice;
                    hr = enumerator->GetDevice(filterId, filterDevice.put());
                    if (SUCCEEDED(hr) && filterDevice)
                    {
                        winrt::com_ptr<IKsControl> ksControl;
                        hr = filterDevice->Activate(__uuidof(IKsControl), CLSCTX_ALL, nullptr, ksControl.put_void());
                        if (SUCCEEDED(hr) && ksControl)
                        {
                            KSPROPERTY prop = {};
                            prop.Set = KSPROPSETID_BtAudio;
                            prop.Id = connect ? 0 : 1;    // ONESHOT_RECONNECT=0, ONESHOT_DISCONNECT=1
                            prop.Flags = KSPROPERTY_TYPE_GET;

                            ULONG bytesReturned = 0;
                            hr = ksControl->KsProperty(&prop, sizeof(prop), nullptr, 0, &bytesReturned);
                            DebugLog(L"[BTDevTray] KsProperty(%s) hr=0x%08lX\n",
                                connect ? L"RECONNECT" : L"DISCONNECT", hr);
                            success = SUCCEEDED(hr);
                        }
                        else
                        {
                            DebugLog(L"[BTDevTray] IKsControl activate failed hr=0x%08lX\n", hr);
                        }
                    }
                }

                CoTaskMemFree(filterId);
            }
        }
    }

    DebugLog(L"[BTDevTray] ReconnectBluetoothAudio result=%d\n", success);
    return success;
}

BluetoothDeviceManager::BluetoothDeviceManager() = default;

BluetoothDeviceManager::~BluetoothDeviceManager()
{
    Stop();
}

void BluetoothDeviceManager::Start()
{
    // Properties to request
    std::vector<winrt::hstring> requestedProperties{
        PROP_IS_CONNECTED,
        PROP_DEVICE_ADDRESS,
        PROP_BATTERY_PERCENT
    };

    auto propList = winrt::single_threaded_vector(std::move(requestedProperties));

    // Classic Bluetooth watcher
    auto classicSelector = WDB::BluetoothDevice::GetDeviceSelectorFromPairingState(true);
    m_classicWatcher = WDE::DeviceInformation::CreateWatcher(
        classicSelector,
        propList,
        WDE::DeviceInformationKind::AssociationEndpoint);

    m_classicAdded = m_classicWatcher.Added({ this, &BluetoothDeviceManager::OnClassicDeviceAdded });
    m_classicUpdated = m_classicWatcher.Updated({ this, &BluetoothDeviceManager::OnClassicDeviceUpdated });
    m_classicRemoved = m_classicWatcher.Removed({ this, &BluetoothDeviceManager::OnClassicDeviceRemoved });

    // BLE watcher
    auto bleSelector = WDB::BluetoothLEDevice::GetDeviceSelectorFromPairingState(true);

    std::vector<winrt::hstring> bleRequestedProperties{
        PROP_IS_CONNECTED,
        PROP_DEVICE_ADDRESS,
        PROP_BATTERY_PERCENT
    };
    auto blePropList = winrt::single_threaded_vector(std::move(bleRequestedProperties));

    m_bleWatcher = WDE::DeviceInformation::CreateWatcher(
        bleSelector,
        blePropList,
        WDE::DeviceInformationKind::AssociationEndpoint);

    m_bleAdded = m_bleWatcher.Added({ this, &BluetoothDeviceManager::OnBleDeviceAdded });
    m_bleUpdated = m_bleWatcher.Updated({ this, &BluetoothDeviceManager::OnBleDeviceUpdated });
    m_bleRemoved = m_bleWatcher.Removed({ this, &BluetoothDeviceManager::OnBleDeviceRemoved });

    m_classicWatcher.Start();
    m_bleWatcher.Start();
}

void BluetoothDeviceManager::Stop()
{
    if (m_classicWatcher)
    {
        auto status = m_classicWatcher.Status();
        if (status == WDE::DeviceWatcherStatus::Started ||
            status == WDE::DeviceWatcherStatus::EnumerationCompleted)
        {
            m_classicWatcher.Stop();
        }
        m_classicWatcher.Added(m_classicAdded);
        m_classicWatcher.Updated(m_classicUpdated);
        m_classicWatcher.Removed(m_classicRemoved);
        m_classicWatcher = nullptr;
    }

    if (m_bleWatcher)
    {
        auto status = m_bleWatcher.Status();
        if (status == WDE::DeviceWatcherStatus::Started ||
            status == WDE::DeviceWatcherStatus::EnumerationCompleted)
        {
            m_bleWatcher.Stop();
        }
        m_bleWatcher.Added(m_bleAdded);
        m_bleWatcher.Updated(m_bleUpdated);
        m_bleWatcher.Removed(m_bleRemoved);
        m_bleWatcher = nullptr;
    }

    // Release all active connections
    m_activeClassic.clear();
    m_activeBle.clear();
}

bool BluetoothDeviceManager::GetIsConnected(
    const winrt::Windows::Foundation::Collections::IMapView<winrt::hstring, WF::IInspectable>& props)
{
    if (props.HasKey(PROP_IS_CONNECTED))
    {
        auto val = props.Lookup(PROP_IS_CONNECTED);
        if (val)
        {
            return winrt::unbox_value<bool>(val);
        }
    }
    return false;
}

static std::optional<uint8_t> GetBatteryFromProperties(
    const winrt::Windows::Foundation::Collections::IMapView<winrt::hstring, WF::IInspectable>& props)
{
    if (props.HasKey(PROP_BATTERY_PERCENT))
    {
        auto val = props.Lookup(PROP_BATTERY_PERCENT);
        if (val)
        {
            try
            {
                uint8_t level = winrt::unbox_value<uint8_t>(val);
                if (level <= 100)
                    return level;
            }
            catch (...) {}
        }
    }
    return std::nullopt;
}

// Classic Bluetooth event handlers
void BluetoothDeviceManager::OnClassicDeviceAdded(WDE::DeviceWatcher, WDE::DeviceInformation info)
{
    DeviceInfo device;
    device.id = info.Id().c_str();
    device.name = info.Name().empty() ? L"Unknown Device" : std::wstring(info.Name().c_str());
    device.type = DeviceType::Classic;
    device.isConnected = GetIsConnected(info.Properties());
    device.bluetoothAddress = GetAddressFromProperties(info.Properties());
    if (device.bluetoothAddress == 0)
        device.bluetoothAddress = ExtractAddressFromAepId(device.id);
    device.batteryLevel = GetBatteryFromProperties(info.Properties());

    DebugLog(L"[BTDevTray] Classic added: '%s' addr=0x%012llX connected=%d battery=%d id='%s'\n",
        device.name.c_str(), device.bluetoothAddress, device.isConnected,
        device.batteryLevel.value_or(255), device.id.c_str());

    {
        std::lock_guard lock(m_mutex);
        m_devices[device.id] = device;
    }
    NotifyChanged();
}

void BluetoothDeviceManager::OnClassicDeviceUpdated(WDE::DeviceWatcher, WDE::DeviceInformationUpdate update)
{
    {
        std::lock_guard lock(m_mutex);
        auto it = m_devices.find(std::wstring(update.Id().c_str()));
        if (it != m_devices.end())
        {
            if (update.Properties().HasKey(PROP_IS_CONNECTED))
            {
                auto val = update.Properties().Lookup(PROP_IS_CONNECTED);
                if (val)
                {
                    bool wasConnected = it->second.isConnected;
                    it->second.isConnected = winrt::unbox_value<bool>(val);
                    it->second.isConnecting = false;

                    // Clear battery level when disconnected
                    if (wasConnected && !it->second.isConnected)
                        it->second.batteryLevel.reset();
                }
            }

            auto battery = GetBatteryFromProperties(update.Properties());
            if (battery.has_value())
                it->second.batteryLevel = battery;
        }
    }
    NotifyChanged();
}

void BluetoothDeviceManager::OnClassicDeviceRemoved(WDE::DeviceWatcher, WDE::DeviceInformationUpdate update)
{
    {
        std::lock_guard lock(m_mutex);
        m_devices.erase(std::wstring(update.Id().c_str()));
    }
    NotifyChanged();
}

// BLE event handlers
void BluetoothDeviceManager::OnBleDeviceAdded(WDE::DeviceWatcher, WDE::DeviceInformation info)
{
    DeviceInfo device;
    device.id = info.Id().c_str();
    device.name = info.Name().empty() ? L"Unknown Device" : std::wstring(info.Name().c_str());
    device.type = DeviceType::BLE;
    device.isConnected = GetIsConnected(info.Properties());
    device.bluetoothAddress = GetAddressFromProperties(info.Properties());
    if (device.bluetoothAddress == 0)
        device.bluetoothAddress = ExtractAddressFromAepId(device.id);
    device.batteryLevel = GetBatteryFromProperties(info.Properties());

    DebugLog(L"[BTDevTray] BLE added: '%s' addr=0x%012llX connected=%d battery=%d id='%s'\n",
        device.name.c_str(), device.bluetoothAddress, device.isConnected,
        device.batteryLevel.value_or(255), device.id.c_str());

    {
        std::lock_guard lock(m_mutex);
        m_devices[device.id] = device;
    }
    NotifyChanged();
}

void BluetoothDeviceManager::OnBleDeviceUpdated(WDE::DeviceWatcher, WDE::DeviceInformationUpdate update)
{
    {
        std::lock_guard lock(m_mutex);
        auto it = m_devices.find(std::wstring(update.Id().c_str()));
        if (it != m_devices.end())
        {
            if (update.Properties().HasKey(PROP_IS_CONNECTED))
            {
                auto val = update.Properties().Lookup(PROP_IS_CONNECTED);
                if (val)
                {
                    bool wasConnected = it->second.isConnected;
                    it->second.isConnected = winrt::unbox_value<bool>(val);
                    it->second.isConnecting = false;

                    if (wasConnected && !it->second.isConnected)
                        it->second.batteryLevel.reset();
                }
            }

            auto battery = GetBatteryFromProperties(update.Properties());
            if (battery.has_value())
                it->second.batteryLevel = battery;
        }
    }
    NotifyChanged();
}

void BluetoothDeviceManager::OnBleDeviceRemoved(WDE::DeviceWatcher, WDE::DeviceInformationUpdate update)
{
    {
        std::lock_guard lock(m_mutex);
        m_devices.erase(std::wstring(update.Id().c_str()));
    }
    NotifyChanged();
}

void BluetoothDeviceManager::ConnectAsync(const std::wstring& deviceId)
{
    DeviceType type;
    uint64_t btAddress = 0;
    {
        std::lock_guard lock(m_mutex);
        auto it = m_devices.find(deviceId);
        if (it == m_devices.end())
            return;
        it->second.isConnecting = true;
        type = it->second.type;
        btAddress = it->second.bluetoothAddress;
    }
    NotifyChanged();

    DebugLog(L"[BTDevTray] ConnectAsync: type=%d addr=0x%012llX id='%s'\n",
        (int)type, btAddress, deviceId.c_str());

    if (btAddress == 0)
    {
        DebugLog(L"[BTDevTray] ConnectAsync: no address, aborting\n");
        std::lock_guard lock(m_mutex);
        auto it = m_devices.find(deviceId);
        if (it != m_devices.end())
            it->second.isConnecting = false;
        return;
    }

    if (type == DeviceType::BLE)
    {
        [](std::wstring devId, uint64_t addr, BluetoothDeviceManager* self) -> winrt::fire_and_forget
        {
            try
            {
                auto device = co_await WDB::BluetoothLEDevice::FromBluetoothAddressAsync(addr);
                if (device)
                {
                    DebugLog(L"[BTDevTray] BLE device obtained, getting GATT services...\n");
                    co_await device.GetGattServicesAsync();
                    // Store reference to keep connection alive
                    std::lock_guard lock(self->m_mutex);
                    self->m_activeBle.insert_or_assign(devId, device);
                    DebugLog(L"[BTDevTray] BLE device stored, connection should persist\n");
                }
                else
                {
                    DebugLog(L"[BTDevTray] BLE FromBluetoothAddressAsync returned null\n");
                }
            }
            catch (...) {
                DebugLog(L"[BTDevTray] BLE connect exception\n");
            }

            {
                std::lock_guard lock(self->m_mutex);
                auto it = self->m_devices.find(devId);
                if (it != self->m_devices.end())
                    it->second.isConnecting = false;
            }
            self->NotifyChanged();
        }(deviceId, btAddress, this);
    }
    else
    {
        [](std::wstring devId, uint64_t addr, BluetoothDeviceManager* self) -> winrt::fire_and_forget
        {
            co_await winrt::resume_background();
            try
            {
                // Use IKsControl to tell the Bluetooth audio driver to reconnect.
                // This works by finding the device's audio endpoint (even in UNPLUGGED state)
                // and sending KSPROPERTY_ONESHOT_RECONNECT to its kernel streaming filter.
                bool connected = ReconnectBluetoothAudio(addr, true);

                if (!connected)
                {
                    DebugLog(L"[BTDevTray] Audio reconnect not available, trying RFCOMM hold...\n");
                    // Fallback for non-audio devices: hold device reference (establishes ACL)
                    auto device = co_await WDB::BluetoothDevice::FromBluetoothAddressAsync(addr);
                    if (device)
                    {
                        co_await device.GetRfcommServicesAsync(WDB::BluetoothCacheMode::Uncached);
                        std::lock_guard lock(self->m_mutex);
                        self->m_activeClassic.insert_or_assign(devId, device);
                        DebugLog(L"[BTDevTray] RFCOMM hold established as fallback\n");
                    }
                }
            }
            catch (...) {
                DebugLog(L"[BTDevTray] Classic connect exception\n");
            }

            {
                std::lock_guard lock(self->m_mutex);
                auto it = self->m_devices.find(devId);
                if (it != self->m_devices.end())
                    it->second.isConnecting = false;
            }
            self->NotifyChanged();
        }(deviceId, btAddress, this);
    }
}

void BluetoothDeviceManager::DisconnectAsync(const std::wstring& deviceId)
{
    uint64_t btAddress = 0;
    DeviceType type = DeviceType::Classic;
    {
        std::lock_guard lock(m_mutex);
        auto it = m_devices.find(deviceId);
        if (it == m_devices.end())
            return;
        btAddress = it->second.bluetoothAddress;
        type = it->second.type;
        it->second.batteryLevel.reset();

        // Release stored device references
        m_activeClassic.erase(deviceId);
        m_activeBle.erase(deviceId);
    }
    NotifyChanged();

    DebugLog(L"[BTDevTray] DisconnectAsync: type=%d addr=0x%012llX\n", (int)type, btAddress);

    if (btAddress != 0)
    {
        // Disconnect audio profile + ACL link on a background thread
        [](uint64_t addr, DeviceType devType) -> winrt::fire_and_forget
        {
            co_await winrt::resume_background();

            if (devType == DeviceType::Classic)
            {
                // Disconnect audio profile via IKsControl
                ReconnectBluetoothAudio(addr, false);
            }

            // Break the ACL link to prevent auto-reconnect
            DisconnectBluetoothAcl(addr);
        }(btAddress, type);
    }
}

void BluetoothDeviceManager::ToggleConnectionAsync(const std::wstring& deviceId)
{
    bool isConnected = false;
    {
        std::lock_guard lock(m_mutex);
        auto it = m_devices.find(deviceId);
        if (it == m_devices.end() || it->second.isConnecting)
            return;
        isConnected = it->second.isConnected;
    }

    if (isConnected)
        DisconnectAsync(deviceId);
    else
        ConnectAsync(deviceId);
}

std::vector<DeviceInfo> BluetoothDeviceManager::GetDevices() const
{
    std::lock_guard lock(m_mutex);
    std::vector<DeviceInfo> result;
    result.reserve(m_devices.size());
    for (const auto& [id, device] : m_devices)
    {
        result.push_back(device);
    }
    std::sort(result.begin(), result.end(), [](const DeviceInfo& a, const DeviceInfo& b)
    {
        // Connected devices first, then alphabetical by name
        if (a.isConnected != b.isConnected)
            return a.isConnected > b.isConnected;
        return a.name < b.name;
    });
    return result;
}

void BluetoothDeviceManager::UpdateBatteryLevel(const std::wstring& deviceId, uint8_t level)
{
    {
        std::lock_guard lock(m_mutex);
        auto it = m_devices.find(deviceId);
        if (it != m_devices.end())
        {
            it->second.batteryLevel = level;
        }
    }
    NotifyChanged();
}

void BluetoothDeviceManager::SetDeviceListChangedCallback(DeviceListChangedCallback callback)
{
    m_changedCallback = std::move(callback);
}

void BluetoothDeviceManager::NotifyChanged()
{
    if (m_changedCallback)
        m_changedCallback();
}
