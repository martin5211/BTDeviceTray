#pragma once

#include "MainWindow.g.h"


namespace winrt::BTDeviceTray::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow() = default;
    };
}

namespace winrt::BTDeviceTray::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
