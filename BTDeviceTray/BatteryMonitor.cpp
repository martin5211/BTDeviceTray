#include "pch.h"
#include "BatteryMonitor.h"
#include <setupapi.h>
#include <devpkey.h>

#pragma comment(lib, "setupapi.lib")

namespace WDB = winrt::Windows::Devices::Bluetooth;
namespace WDBG = winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;
namespace WSS = winrt::Windows::Storage::Streams;

// GATT Battery Service UUID: 0x180F
static const winrt::guid BATTERY_SERVICE_UUID{ 0x0000180F, 0x0000, 0x1000, { 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB } };
// GATT Battery Level Characteristic UUID: 0x2A19
static const winrt::guid BATTERY_LEVEL_UUID{ 0x00002A19, 0x0000, 0x1000, { 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB } };

static void DebugLog(const wchar_t* fmt, ...)
{
    wchar_t buf[512];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, args);
    va_end(args);
    OutputDebugStringW(buf);
}

// Known property keys that may contain Bluetooth device battery percentage
static const DEVPROPKEY s_batteryKeys[] = {
    // PKEY_Devices_BatteryPercent
    {{0x104EA319, 0x6EE2, 0x4701, {0xBD, 0x47, 0x8D, 0xDB, 0xF4, 0x25, 0xBB, 0xE5}}, 2},
    // DEVPKEY_Bluetooth_Battery (variant)
    {{0x2BD67D8B, 0x8BEB, 0x48D5, {0x87, 0xE0, 0x6C, 0xDA, 0x34, 0x28, 0x04, 0x0A}}, 2},
    // PKEY_Devices_BatteryLife
    {{0x49CD1F76, 0x5626, 0x4B17, {0xA4, 0xE8, 0x18, 0xB4, 0xAA, 0x1A, 0x22, 0x13}}, 10},
    // PKEY_Devices_BatteryPlusCharging
    {{0x49CD1F76, 0x5626, 0x4B17, {0xA4, 0xE8, 0x18, 0xB4, 0xAA, 0x1A, 0x22, 0x13}}, 22},
};

// Read battery percentage from PnP device properties (SetupDi).
// Windows stores HFP-reported battery here for Classic Bluetooth audio devices.
static std::optional<uint8_t> GetBatteryFromPnP(uint64_t btAddress, bool logDiagnostics)
{
    if (btAddress == 0)
        return std::nullopt;

    wchar_t addrHex[13];
    _snwprintf_s(addrHex, _countof(addrHex), _TRUNCATE, L"%012llX", btAddress);
    std::wstring addrUpper(addrHex);

    HDEVINFO devInfoSet = SetupDiGetClassDevsW(
        nullptr, L"BTHENUM", nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (devInfoSet == INVALID_HANDLE_VALUE)
        return std::nullopt;

    SP_DEVINFO_DATA devInfoData = {};
    devInfoData.cbSize = sizeof(devInfoData);
    std::optional<uint8_t> result;

    for (DWORD i = 0; SetupDiEnumDeviceInfo(devInfoSet, i, &devInfoData) && !result; ++i)
    {
        wchar_t instanceId[512];
        if (!SetupDiGetDeviceInstanceIdW(devInfoSet, &devInfoData, instanceId, _countof(instanceId), nullptr))
            continue;

        std::wstring idStr(instanceId);
        std::transform(idStr.begin(), idStr.end(), idStr.begin(), ::towupper);
        if (idStr.find(addrUpper) == std::wstring::npos)
            continue;

        // Try known battery property keys
        for (const auto& key : s_batteryKeys)
        {
            DEVPROPTYPE propType = 0;
            BYTE buffer[4] = {};
            DWORD requiredSize = 0;

            if (SetupDiGetDevicePropertyW(devInfoSet, &devInfoData, &key, &propType,
                buffer, sizeof(buffer), &requiredSize, 0))
            {
                uint8_t level = 0;
                if (propType == DEVPROP_TYPE_BYTE && requiredSize >= 1)
                    level = buffer[0];
                else if (propType == DEVPROP_TYPE_UINT16 && requiredSize >= 2)
                    level = static_cast<uint8_t>(*reinterpret_cast<uint16_t*>(buffer));
                else if (propType == DEVPROP_TYPE_UINT32 && requiredSize >= 4)
                    level = static_cast<uint8_t>(*reinterpret_cast<uint32_t*>(buffer));
                else
                    continue;

                if (level > 0 && level <= 100)
                {
                    DebugLog(L"[BTDevTray] PnP battery: %u%% on %s\n", level, instanceId);
                    result = level;
                    break;
                }
            }
        }

        // Diagnostic: log all numeric properties so we can find the right key
        if (!result && logDiagnostics)
        {
            DebugLog(L"[BTDevTray] PnP device: %s - enumerating properties\n", instanceId);

            DWORD propCount = 0;
            SetupDiGetDevicePropertyKeys(devInfoSet, &devInfoData, nullptr, 0, &propCount, 0);
            if (propCount > 0)
            {
                std::vector<DEVPROPKEY> propKeys(propCount);
                if (SetupDiGetDevicePropertyKeys(devInfoSet, &devInfoData,
                    propKeys.data(), propCount, &propCount, 0))
                {
                    for (const auto& pk : propKeys)
                    {
                        DEVPROPTYPE propType = 0;
                        BYTE buffer[256] = {};
                        DWORD requiredSize = 0;

                        if (!SetupDiGetDevicePropertyW(devInfoSet, &devInfoData, &pk, &propType,
                            buffer, sizeof(buffer), &requiredSize, 0))
                            continue;

                        if (propType == DEVPROP_TYPE_BYTE ||
                            propType == DEVPROP_TYPE_UINT16 ||
                            propType == DEVPROP_TYPE_UINT32 ||
                            propType == DEVPROP_TYPE_INT32)
                        {
                            uint32_t val = 0;
                            if (propType == DEVPROP_TYPE_BYTE) val = buffer[0];
                            else if (propType == DEVPROP_TYPE_UINT16) val = *reinterpret_cast<uint16_t*>(buffer);
                            else val = *reinterpret_cast<uint32_t*>(buffer);

                            DebugLog(L"[BTDevTray]   {%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X},%u t=%lu v=%u\n",
                                pk.fmtid.Data1, pk.fmtid.Data2, pk.fmtid.Data3,
                                pk.fmtid.Data4[0], pk.fmtid.Data4[1],
                                pk.fmtid.Data4[2], pk.fmtid.Data4[3],
                                pk.fmtid.Data4[4], pk.fmtid.Data4[5],
                                pk.fmtid.Data4[6], pk.fmtid.Data4[7],
                                pk.pid, propType, val);
                        }
                    }
                }
            }
        }
    }

    SetupDiDestroyDeviceInfoList(devInfoSet);
    return result;
}

BatteryMonitor::BatteryMonitor() = default;

BatteryMonitor::~BatteryMonitor()
{
    Stop();
}

void BatteryMonitor::Start(DeviceSnapshotProvider provider)
{
    m_deviceProvider = std::move(provider);
    m_running = true;
    PollLoop();
}

void BatteryMonitor::Stop()
{
    m_running = false;
}

void BatteryMonitor::SetBatteryUpdatedCallback(BatteryUpdatedCallback callback)
{
    m_batteryCallback = std::move(callback);
}

winrt::fire_and_forget BatteryMonitor::PollLoop()
{
    // Wait for DeviceWatcher to finish initial enumeration
    co_await winrt::resume_after(std::chrono::seconds(5));

    bool firstPoll = true;

    while (m_running)
    {
        if (m_deviceProvider)
        {
            auto devices = m_deviceProvider();

            for (const auto& device : devices)
            {
                if (!m_running)
                    break;

                if (!device.isConnected)
                    continue;

                // Skip devices that already have battery from the DeviceWatcher
                if (device.batteryLevel.has_value())
                    continue;

                bool gotBattery = false;

                // Strategy 1: GATT Battery Service (BLE and dual-mode devices)
                try
                {
                    WDB::BluetoothLEDevice bleDevice{ nullptr };

                    if (device.type == DeviceType::BLE)
                    {
                        bleDevice = co_await WDB::BluetoothLEDevice::FromIdAsync(device.id);
                    }
                    else if (device.bluetoothAddress != 0)
                    {
                        bleDevice = co_await WDB::BluetoothLEDevice::FromBluetoothAddressAsync(
                            device.bluetoothAddress);
                    }

                    if (bleDevice)
                    {
                        auto servicesResult = co_await bleDevice.GetGattServicesForUuidAsync(
                            BATTERY_SERVICE_UUID,
                            WDB::BluetoothCacheMode::Cached);

                        if (servicesResult.Status() != WDBG::GattCommunicationStatus::Success ||
                            servicesResult.Services().Size() == 0)
                        {
                            servicesResult = co_await bleDevice.GetGattServicesForUuidAsync(
                                BATTERY_SERVICE_UUID,
                                WDB::BluetoothCacheMode::Uncached);
                        }

                        if (servicesResult.Status() == WDBG::GattCommunicationStatus::Success &&
                            servicesResult.Services().Size() > 0)
                        {
                            auto charsResult = co_await servicesResult.Services().GetAt(0)
                                .GetCharacteristicsForUuidAsync(
                                    BATTERY_LEVEL_UUID,
                                    WDB::BluetoothCacheMode::Cached);

                            if (charsResult.Status() != WDBG::GattCommunicationStatus::Success ||
                                charsResult.Characteristics().Size() == 0)
                            {
                                charsResult = co_await servicesResult.Services().GetAt(0)
                                    .GetCharacteristicsForUuidAsync(
                                        BATTERY_LEVEL_UUID,
                                        WDB::BluetoothCacheMode::Uncached);
                            }

                            if (charsResult.Status() == WDBG::GattCommunicationStatus::Success &&
                                charsResult.Characteristics().Size() > 0)
                            {
                                auto readResult = co_await charsResult.Characteristics().GetAt(0)
                                    .ReadValueAsync(WDB::BluetoothCacheMode::Uncached);

                                if (readResult.Status() == WDBG::GattCommunicationStatus::Success)
                                {
                                    auto reader = WSS::DataReader::FromBuffer(readResult.Value());
                                    uint8_t level = reader.ReadByte();
                                    if (level <= 100 && m_batteryCallback)
                                    {
                                        m_batteryCallback(device.id, level);
                                        gotBattery = true;
                                    }
                                }
                            }
                        }
                    }
                }
                catch (...) {}

                // Strategy 2: PnP device properties (Classic BT with HFP battery)
                if (!gotBattery && device.bluetoothAddress != 0)
                {
                    co_await winrt::resume_background();
                    auto level = GetBatteryFromPnP(device.bluetoothAddress, firstPoll);
                    if (level.has_value() && m_batteryCallback)
                    {
                        m_batteryCallback(device.id, level.value());
                        gotBattery = true;
                    }
                }
            }
        }

        firstPoll = false;

        // Wait 30 seconds before next poll
        co_await winrt::resume_after(std::chrono::seconds(30));
    }
}
