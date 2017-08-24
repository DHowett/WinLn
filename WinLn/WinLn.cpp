#include "common.h"
#include "error.h"
#include "getopt.h"

#include <Shlwapi.h>
#include <stdio.h>
#include <string>

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

static std::wstring WlnMakePathAbsolute(const std::wstring& path) {
	wchar_t buf[LONG_MAX_PATH];
	if(!GetFullPathNameW(path.c_str(), std::extent<decltype(buf)>::value, buf, nullptr)) {
		WlnAbortWithWin32Error(GetLastError(), L"Failed to locate `%ls' relative to cwd.", path.c_str());
	}
	return buf;
}

// WlnMakePathAbsoluteAsDirectory is like WlnMakePathAbsolute, but it
// strips off the final filename and returns only the directory.
static std::wstring WlnMakePathAbsoluteAsDirectory(const std::wstring& path) {
	wchar_t buf[LONG_MAX_PATH];
	wchar_t* filename = nullptr;
	if(!GetFullPathNameW(path.c_str(), std::extent<decltype(buf)>::value, buf, &filename)) {
		WlnAbortWithWin32Error(GetLastError(), L"Failed to locate `%ls' relative to cwd.", path.c_str());
	}
	*filename = L'\0';
	return buf;
}

static std::wstring WlnMakePathRelative(const std::wstring& path, const std::wstring& to, bool isDir) {
	wchar_t rel[LONG_MAX_PATH];
	if(!PathRelativePathToW(rel, to.c_str(), FILE_ATTRIBUTE_DIRECTORY, path.c_str(), isDir ? FILE_ATTRIBUTE_DIRECTORY : 0)) {
		WlnAbortWithReason(L"Could not make `%ls' relative to `%ls'.", path.c_str(), to.c_str());
	}
	return rel;
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

	if(relative && (!symlink && !junction)) {
		WlnAbortWithArgumentError(L"cannot do --relative without --symbolic or --junction");
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
			std::wstring tabs = WlnMakePathAbsolute(target);
			std::wstring lbase = WlnMakePathAbsoluteAsDirectory(linkname);
			target = WlnMakePathRelative(tabs, lbase, targetFi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
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

