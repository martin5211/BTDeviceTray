#pragma once

// Windows SDK
#include <windows.h>
#include <shellapi.h>
#include <bluetoothapis.h>
#include <mmdeviceapi.h>
#include <devicetopology.h>
#include <unknwn.h>
#include <restrictederrorinfo.h>
#include <hstring.h>

// Undefine GetCurrentTime macro to avoid conflicts with WinRT
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

// WinRT / C++/WinRT
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>

// Bluetooth WinRT APIs
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Devices.Bluetooth.Rfcomm.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Media.Audio.h>

// Standard library
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <optional>
#include <functional>
#include <algorithm>
#include <memory>
