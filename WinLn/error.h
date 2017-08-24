#pragma once

#include <string>

__declspec(noreturn) void WlnAbortWithReason(const wchar_t* fmt, ...);
__declspec(noreturn) void WlnAbortWithWin32Error(int err, const wchar_t* fmt, ...);
__declspec(noreturn) void WlnAbortWithArgumentError(const wchar_t* fmt, ...);
const std::wstring& WlnGetProgName();