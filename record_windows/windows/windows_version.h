#pragma once

#define NOMINMAX
#include <windows.h>
#include <versionhelpers.h>

namespace record_windows
{
    enum class WindowsVersion
    {
        Windows7,
        Windows8,
        Windows10Plus
    };

    // 检测Windows版本
    WindowsVersion GetWindowsVersion();

    // 检查是否是Windows 7
    bool IsWindows7();

    // 检查是否是Windows 10或更高版本
    bool IsWindows10Plus();
} 