#pragma once
#include "XamlMetaDataProvider.g.h"

namespace winrt::BTDeviceTray::implementation
{
    struct XamlMetaDataProvider : XamlMetaDataProviderT<XamlMetaDataProvider>
    {
        XamlMetaDataProvider() = default;

        winrt::Microsoft::UI::Xaml::Markup::IXamlType GetXamlType(winrt::Windows::UI::Xaml::Interop::TypeName const&)
        {
            return nullptr;
        }
        winrt::Microsoft::UI::Xaml::Markup::IXamlType GetXamlType(hstring const&)
        {
            return nullptr;
        }
        com_array<winrt::Microsoft::UI::Xaml::Markup::XmlnsDefinition> GetXmlnsDefinitions()
        {
            return {};
        }
    };
}
namespace winrt::BTDeviceTray::factory_implementation
{
    struct XamlMetaDataProvider : XamlMetaDataProviderT<XamlMetaDataProvider, implementation::XamlMetaDataProvider>
    {
    };
}
