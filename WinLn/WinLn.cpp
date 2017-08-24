#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Shlwapi.h>
#include <stdio.h>
#include <string>
#include <memory>

#define LONG_MAX_PATH 32768

__declspec(noreturn) static void WlnAbortWithReason(const wchar_t* fmt, ...);
__declspec(noreturn) static void WlnAbortWithWin32Error(int err, const wchar_t* fmt, ...);
__declspec(noreturn) static void WlnAbortWithArgumentError(const wchar_t* fmt, ...);

static const std::wstring& WlnGetProgName() {
	static std::wstring fn = []()->auto {
		wchar_t fn[LONG_MAX_PATH];
		if(!GetModuleFileNameW(nullptr, fn, std::extent<decltype(fn)>::value)) {
			WlnAbortWithWin32Error(GetLastError(), L"Failed to determine launch path.");
		}
		return std::wstring{wcsrchr(fn, L'\\') + 1};
	}();
	return fn;
}

__declspec(noreturn) static void WlnAbortWithUsage() {
	fwprintf(stderr, L"Usage: %ls [option]... <target> <link>\r\n"
		             L"\r\n"
		             L"  -s, --symbolic     create symbolic links instead of hard links\r\n"
		             L"  -j, --junction     create a Windows directory junctions instead of hard links\r\n"
		             L"                     -s and -j are mutually exclusive\r\n"
		             L"\r\n"
		             L"  -r, --relative     create symbolic links relative to link location\r\n"
		             L"  -f, --force        remove existing destination files\r\n"
		             L"  -h, --help         display this help\r\n"
		, WlnGetProgName().c_str());
	exit(0);
}

__declspec(noreturn) static void WlnAbortWithArgumentError(const wchar_t* fmt, ...) {
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

__declspec(noreturn) static void WlnAbortWithReason(const wchar_t* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vfwprintf(stderr, fmt, ap);
	va_end(ap);

	exit(1);
}

__declspec(noreturn) static void WlnAbortWithWin32Error(int err, const wchar_t* fmt, ...) {
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

struct option {
	const wchar_t* name;
	wchar_t shopt;
	bool has_arg;
};

static int getopt_long(int argc, wchar_t** argv, const option opts[], int* idxptr, wchar_t** optargptr) {
	int& idx = *idxptr;
	if(idx == 0) idx = 1; // skip progname

	int fa = idx;
	for(; fa < argc && argv[fa][0] != L'-'; ++fa);

	if(fa == argc) {
		return -1; // no more args, we're done!
	}

	// found the first argument.
	wchar_t* arg = argv[fa];
	const option* foundopt = nullptr;
	wchar_t* foundarg = nullptr;
	int start = 1 + static_cast<int>(arg[1] == L'-');
	wchar_t* eq = start == 2 ? wcschr(arg, L'=') : nullptr;
	int arglen = eq ? eq - arg - start : wcslen(arg) - start;

	for(const option* o = opts; o; ++o) {
		if(o->name == nullptr) {
			break; // arg not found! opterr?
		}

		if((start == 1 && (arg[start] != o->shopt || arg[start + 1] != L'\0'))
		|| (start == 2 && wcsncmp(o->name, arg + start, arglen) != 0)) {
			continue; // next arg
		}

		if(start == 2 && eq) {
			if(!o->has_arg) break;
			foundarg = arg + (start + arglen + 1); // account for -- and =
		} else if(o->has_arg) {
			int argpos = fa + 1;
			if(argpos >= argc) {
				break; // arg missing arg: opterr?
			}
			foundarg = argv[argpos];
		}
		foundopt = o;
		break;
	}

	// "1 -a 2" -> "-a 2 1"
	// move everything between idx and fa (or fa+1) over end of fa.
	int nnonargs = fa - idx; // num non args
	memmove(argv + fa + 1 + static_cast<int>(foundarg && !eq) - nnonargs, argv + idx, nnonargs * sizeof(wchar_t*));
	argv[idx] = arg; ++idx;
	if(foundarg && !eq) {
		argv[idx] = foundarg;
		++idx;
	}

	if(optargptr) *optargptr = foundarg;
	return foundopt ? foundopt->shopt : L'?';
}

static option opts[]{
	{L"force", L'f', false},
	{L"symbolic", L's', false},
	{L"junction", L'j', false},
	{L"relative", L'r', false},
	{L"help", L'h', false},
	//{L"no-target-directory", L'T', false},
	{nullptr, 0, false},
};

int wmain(int argc, wchar_t** argv) {
	bool force = false, symlink = false, junction = false, relative = false;
	int optind = 0;
	while(int o = getopt_long(argc, argv, opts, &optind, nullptr)) {
		switch(o) {
		case -1:
			goto opts_done;
		case 'f':
			force = true;
			break;
		case 'h':
			WlnAbortWithUsage();
			return 0;
		case 'j':
			junction = true;
			break;
		case 'r':
			relative = true;
			break;
		case 's':
			symlink = true;
			break;
		}
	}
opts_done:

	if(symlink && junction) {
		WlnAbortWithArgumentError(L"cannot do --symbolic and --junction at the same time");
		return 1;
	}

	if(relative && !symlink) {
		WlnAbortWithArgumentError(L"cannot do --relative without --symbolic");
		return 1;

	}

	if(optind + 2 > argc) {
		WlnAbortWithArgumentError(L"missing file operand");
		return 1;
	}

	std::wstring target{argv[optind]};
	std::wstring linkname{argv[optind + 1]};

	WIN32_FILE_ATTRIBUTE_DATA targetFi{};
	if(!GetFileAttributesExW(target.c_str(), GetFileExInfoStandard, &targetFi)) {
		WlnAbortWithWin32Error(GetLastError(), L"Failed to read attributes for `%ls'.", target.c_str());
	}

	WIN32_FILE_ATTRIBUTE_DATA linkFi{};
	if(int r = GetFileAttributesExW(linkname.c_str(), GetFileExInfoStandard, &linkFi)) {
		if(!r && GetLastError() != ERROR_NOT_FOUND) {
			WlnAbortWithWin32Error(GetLastError(), L"Failed to read attributes for `%ls'.", linkname.c_str());
		}
		else if(r && force) {
			if(!DeleteFileW(linkname.c_str())) {
				WlnAbortWithWin32Error(GetLastError(), L"Failed to delete `%ls'.", linkname.c_str());
			}
		}
	}

	if(symlink) {
		if(relative) {
			WlnAbortWithArgumentError(L"--relative is not yet implemented");
			return 1;
		}

		int flags = 0;
		if((targetFi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
			flags |= SYMBOLIC_LINK_FLAG_DIRECTORY;
		}

		if(!CreateSymbolicLinkW(linkname.c_str(), target.c_str(), flags | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
			WlnAbortWithWin32Error(GetLastError(), nullptr);
		}
	} else if(junction) {
		// TODO(DH): Junctions
		WlnAbortWithArgumentError(L"junctions aren't supported");
		return 1;
	} else {
		if(!CreateHardLinkW(linkname.c_str(), target.c_str(), nullptr)) {
			WlnAbortWithWin32Error(GetLastError(), nullptr);
		}
	}

	return 0;
}

