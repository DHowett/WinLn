#define _SCL_SECURE_NO_WARNINGS 1
#include "common.h"
#include "error.h"
#include "getopt.h"

#include <winioctl.h>
#include <Shlwapi.h>
#include <stdio.h>
#include <string>

struct REPARSE_POINT_HEADER {
	ULONG ReparseTag;
	USHORT ReparseDataLength;
	USHORT Reserved;
};

struct REPARSE_MOUNT_POINT_BUFFER {
	REPARSE_POINT_HEADER Header;
	USHORT SubstituteNameOffset;
	USHORT SubstituteNameLength;
	USHORT PrintNameOffset;
	USHORT PrintNameLength;
	WCHAR  PathBuffer[0];
};

__declspec(noreturn) static void WlnAbortWithUsage() {
	fwprintf(stderr, L"Usage: %ls [option]... <target> <link>\r\n"
		             L"\r\n"
		             L"  -s, --symbolic     create symbolic links instead of hard links\r\n"
		             L"  -j, --junction     create Windows directory junctions instead of hard links\r\n"
		             L"                     -s and -j are mutually exclusive\r\n"
		             L"\r\n"
		             L"  -r, --relative     create symbolic links relative to link location\r\n"
		             L"  -f, --force        remove existing destination files\r\n"
		             L"  -h, --help         display this help\r\n"
		, WlnGetProgName().c_str());
	exit(0);
}

static std::wstring WlnMakePathAbsolute(const std::wstring& path) {
	if(path.compare(0, 4, L"\\??\\") == 0) return path;

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
		int gle = 0;
		// Symbolic Links and Junctions can be created for nonexistent targets.
		if((!symlink && !junction) || (gle = GetLastError()) != ERROR_FILE_NOT_FOUND) {
			WlnAbortWithWin32Error(GetLastError(), L"Failed to read attributes for `%ls'.", target.c_str());
		}
	}

	WIN32_FILE_ATTRIBUTE_DATA linkFi{};
	int ret = GetFileAttributesExW(linkname.c_str(), GetFileExInfoStandard, &linkFi);
	int gle = 0;
	if(!ret && (gle = GetLastError()) != ERROR_FILE_NOT_FOUND) {
		WlnAbortWithWin32Error(gle, L"Failed to read attributes for `%ls'.", linkname.c_str());
	} else if(ret && force) {
		int ret = 0;
		if(linkFi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY && linkFi.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
			// directories that are reparse points
			ret = RemoveDirectoryW(linkname.c_str());
		} else if(!(linkFi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
			// files (incl. reparse points)
			ret = DeleteFileW(linkname.c_str());
		} else {
			WlnAbortWithReason(L"Cannot overwrite directory `%ls'.", linkname.c_str());
		}

		if(!ret) {
			WlnAbortWithWin32Error(GetLastError(), L"Failed to delete `%ls'.", linkname.c_str());
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
		std::wstring tabs = WlnMakePathAbsolute(target);
		if(tabs.compare(0, 4, L"\\\\?\\") == 0) {
			tabs[1] = L'?'; // Replace "\\?\" with "\??\"
		}

		if(tabs.compare(0, 4, L"\\??\\") != 0) {
			tabs = L"\\??\\" + tabs;
		}

		if(!CreateDirectoryW(linkname.c_str(), nullptr)) {
			WlnAbortWithWin32Error(GetLastError(), L"Failed to create junction `%ls'.", linkname.c_str());
		}

		HANDLE hFile = INVALID_HANDLE_VALUE;
		if(INVALID_HANDLE_VALUE == (hFile = CreateFileW(linkname.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr))) {
			WlnAbortWithWin32Error(GetLastError(), L"Failed to open junction `%ls'.", linkname.c_str());
		}

		size_t reparseLength = sizeof(REPARSE_MOUNT_POINT_BUFFER) + (tabs.length() * sizeof(wchar_t)) + (2 * sizeof(wchar_t));
		REPARSE_MOUNT_POINT_BUFFER* reparse = static_cast<REPARSE_MOUNT_POINT_BUFFER*>(calloc(1, reparseLength));
		reparse->Header.ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
		reparse->Header.ReparseDataLength = static_cast<uint16_t>(reparseLength - sizeof(REPARSE_POINT_HEADER));
		reparse->SubstituteNameLength = static_cast<uint16_t>(tabs.length() * sizeof(wchar_t));
		reparse->PrintNameOffset = static_cast<uint16_t>(reparse->SubstituteNameLength + sizeof(wchar_t));
		reparse->PrintNameLength = 0;

		std::copy(tabs.begin(), tabs.end(), static_cast<wchar_t*>(reparse->PathBuffer));

		if(!DeviceIoControl(hFile, FSCTL_SET_REPARSE_POINT, reparse, reparseLength, nullptr, 0, nullptr, nullptr)) {
			CloseHandle(hFile);
			WlnAbortWithWin32Error(GetLastError(), L"Failed to populate reparse point at `%ls'.", linkname.c_str());
		}
		CloseHandle(hFile);
	} else {
		if(!CreateHardLinkW(linkname.c_str(), target.c_str(), nullptr)) {
			WlnAbortWithWin32Error(GetLastError(), nullptr);
		}
	}

	return 0;
}

