#define NOMINMAX
#include "windows_version.h"

namespace record_windows
{
    WindowsVersion GetWindowsVersion()
    {
        if (IsWindows10OrGreater())
        {
            return WindowsVersion::Windows10Plus;
        }
        else if (IsWindows8OrGreater())
        {
            return WindowsVersion::Windows8;
        }
        else
        {
            return WindowsVersion::Windows7;
        }
    }

    bool IsWindows7()
    {
        return GetWindowsVersion() == WindowsVersion::Windows7;
    }

    bool IsWindows10Plus()
    {
        return GetWindowsVersion() == WindowsVersion::Windows10Plus;
    }
} 