#pragma once

#include <windows.h>

// {672346F8-A46B-4DC4-B9A9-8322DA98056D}
inline constexpr CLSID CLSID_SonulabASIO = {
    0x672346f8, 0xa46b, 0x4dc4, {0xb9, 0xa9, 0x83, 0x22, 0xda, 0x98, 0x05, 0x6d}
};

inline constexpr wchar_t kSonulabAsioRegistryName[] = L"Sonulab ASIO";
inline constexpr wchar_t kSonulabAsioDescription[] = L"Sonulab StompPRO USB ASIO";
inline constexpr wchar_t kSonulabUsbHardwareId[] = L"VID_1D6B&PID_0104";

