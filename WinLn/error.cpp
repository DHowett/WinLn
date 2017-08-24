#include "common.h"
#include "error.h"

#include <memory>
#include <string>
#include <cstdio>

const std::wstring& WlnGetProgName() {
	static std::wstring fn = []()->auto {
		wchar_t fn[LONG_MAX_PATH];
		if(!GetModuleFileNameW(nullptr, fn, std::extent<decltype(fn)>::value)) {
			WlnAbortWithWin32Error(GetLastError(), L"Failed to determine launch path.");
		}
		return std::wstring{wcsrchr(fn, L'\\') + 1};
	}();
	return fn;
}

__declspec(noreturn) void WlnAbortWithArgumentError(const wchar_t* fmt, ...) {
	fwprintf(stderr, L"%ls: ", WlnGetProgName().c_str());
	va_list ap;
	va_start(ap, fmt);
	vfwprintf(stderr, fmt, ap);
	va_end(ap);
	fwprintf(stderr, L"\r\nTry `%ls --help' for more information.\r\n", WlnGetProgName().c_str());
	exit(1);
}

template <typename T>
static void _heapFree(T* ptr) {
	HeapFree(GetProcessHeap(), 0, ptr);
}

__declspec(noreturn) void WlnAbortWithReason(const wchar_t* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vfwprintf(stderr, fmt, ap);
	va_end(ap);

	exit(1);
}

__declspec(noreturn) void WlnAbortWithWin32Error(int err, const wchar_t* fmt, ...) {
	std::unique_ptr<wchar_t, void(*)(wchar_t*)> buf(nullptr, _heapFree<wchar_t>);
	if(err) {
		wchar_t* b = nullptr;
		FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, nullptr, err, 0, (LPWSTR)&b, 1024, nullptr);
		buf.reset(b);
	}
	if(fmt) {
		va_list ap;
		va_start(ap, fmt);
		vfwprintf(stderr, fmt, ap);
		va_end(ap);
	}
	if(buf) {
		fwprintf(stderr, L"Error 0x%8.08X: %ls", err, buf.get()); // FormatMessageW emits \r\n
	}

	exit(1);
}
