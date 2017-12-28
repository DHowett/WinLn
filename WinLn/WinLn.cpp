#define _SCL_SECURE_NO_WARNINGS 1
#include "common.h"
#include "error.h"

#include <winioctl.h>
#include <Shlwapi.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <getopt/getopt.h>
#include <optional>

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

enum DirOption {
	DirOptionTargetIsFile = -1,
	DirOptionTargetDontCare = 0,
	DirOptionTargetIsDir = 1,
};

enum LinkType {
	LinkTypeHard = 0,
	LinkTypeSymbolic,
	LinkTypeJunction,
};

__declspec(noreturn) static void WlnAbortWithUsage() {
	fwprintf(stderr, L"Usage: %ls [option]... [-T] <target> <link>\r\n"
		L"  or:  %ls [option]... <target>\r\n"
		L"  or:  %ls [option]... <target...> <directory>\r\n"
		L"  or:  %ls [option]... -t <directory> <target>\r\n"
		L"\r\n"
		L"  -s, --symbolic                      create symbolic links instead of hard links\r\n"
		L"  -j, --junction                      create Windows directory junctions instead of hard links\r\n"
		L"                                      -s and -j cannot be used at the same time\r\n"
		L"\r\n"
		L"  -r, --relative                      create symbolic links relative to link location\r\n"
		L"  -f, --force                         remove existing destination files\r\n"
		L"  -t, --target-directory=<directory>  specify the <directory> in which to create links\r\n"
		L"  -T, --no-target-directory           never treat <link> as a directory\r\n"
		L"\r\n"
		L"  -v, --verbose                       print the name of each linked file\r\n"
		L"\r\n"
		L"  -h, --help         display this help\r\n"
		, WlnGetProgName().c_str()
		, WlnGetProgName().c_str()
		, WlnGetProgName().c_str()
		, WlnGetProgName().c_str()
	);
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

static std::optional<WIN32_FILE_ATTRIBUTE_DATA> WlnGetAttributes(const std::wstring& path) {
	WIN32_FILE_ATTRIBUTE_DATA fi{};
	int ret = GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fi);
	int gle = GetLastError();
	if(!ret && gle != ERROR_FILE_NOT_FOUND) {
		WlnAbortWithWin32Error(gle, L"Failed to read attributes for `%ls'.", path.c_str());
	} else if(ret) {
		return {fi};
	}
	return {};
}

static std::optional<FILE_ID_INFO> WlnGetFileID(const std::wstring& path) {
	HANDLE hFile{CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
	int gle = GetLastError();
	if(hFile == INVALID_HANDLE_VALUE && gle != ERROR_FILE_NOT_FOUND) {
		WlnAbortWithWin32Error(gle, L"Failed to open `%ls' for identification.", path.c_str());
	} else if(hFile) {
		FILE_ID_INFO id{};
		auto ret = GetFileInformationByHandleEx(hFile, FileIdInfo, &id, sizeof(id));
		gle = GetLastError();
		CloseHandle(hFile);
		if(!ret) {
			WlnAbortWithWin32Error(gle, L"Failed to get an ID for `%ls'.", path.c_str());
		}
		return {id};
	}
	return {};
}

static bool WlnIsSameFile(const std::optional<FILE_ID_INFO>& left, const std::optional<FILE_ID_INFO>& right) {
	if((!left && !right) || !left || !right) return false;
	auto& lid = left.value();
	auto& rid = right.value();
	return lid.VolumeSerialNumber == rid.VolumeSerialNumber && memcmp(&lid.FileId.Identifier[0], &rid.FileId.Identifier[0], sizeof(lid.FileId)) == 0;
}

static bool WlnIsDirectory(const WIN32_FILE_ATTRIBUTE_DATA& fileInfo) {
	return fileInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
}

static bool WlnIsDirectory(const std::optional<WIN32_FILE_ATTRIBUTE_DATA>& fileInfo) {
	return fileInfo.has_value() ? WlnIsDirectory(fileInfo.value()) : false;
}

static bool WlnIsPhysicalDirectory(const WIN32_FILE_ATTRIBUTE_DATA& fileInfo) {
	return fileInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY && !(fileInfo.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT);
}

static bool WlnIsPhysicalDirectory(const std::optional<WIN32_FILE_ATTRIBUTE_DATA>& fileInfo) {
	return fileInfo.has_value() ? WlnIsPhysicalDirectory(fileInfo.value()) : false;
}

static std::wstring WlnGetFilename(const std::wstring& path) {
	auto pos{path.length() - 2}; // 2: skip a potential final / or \. safe because leaf can't be < 1 char in length
	auto last{path.find_last_of(L"\\/", pos)};
	if(last == std::wstring::npos) {
		last = -1;
	}

	auto leaf{path.substr(last + 1, pos - last + ((path.back() == L'\\' || path.back() == '/') ? 0 : 1))};
	if(leaf.length() == 2 && leaf[1] == L':') {
		// SPECIAL CASE: The filename for a drive (C:\) is its name (C)
		return leaf.substr(0, 1);
	}
	return leaf;
}

static option opts[]{
	{L"force", L'f', false},
	{L"symbolic", L's', false},
	{L"junction", L'j', false},
	{L"relative", L'r', false},
	{L"help", L'h', false},
	{L"no-target-directory", L'T', false},
	{L"target-directory", L't', true},
	{L"verbose", L'v', false},
	{nullptr, 0, false},
};

static void WlnCreateLink(LinkType type, DirOption diropt, const std::wstring& target, std::wstring link, bool force, bool relative, bool verbose, const std::optional<WIN32_FILE_ATTRIBUTE_DATA>& linkFileInfo = {});

int wmain(int argc, wchar_t** argv) {
	bool force = false, relative = false, verbose = false;
	DirOption diropt = DirOptionTargetDontCare;
	LinkType linktyp = LinkTypeHard;
	std::optional<std::wstring> linkname;
	while(int o = getopt_long(argc, argv, opts)) {
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
			if(linktyp == LinkTypeSymbolic) WlnAbortWithArgumentError(L"cannot use --junction with --symbolic");
			linktyp = LinkTypeJunction;
			break;
		case 'r':
			relative = true;
			break;
		case 's':
			if(linktyp == LinkTypeJunction) WlnAbortWithArgumentError(L"cannot use --symbolic with --junction");
			linktyp = LinkTypeSymbolic;
			break;
		case 'T':
			if(diropt == DirOptionTargetIsDir) WlnAbortWithArgumentError(L"cannot use --no-target-directory with --target-directory=");
			diropt = DirOptionTargetIsFile;
			break;
		case 't':
			if(diropt == DirOptionTargetIsFile) WlnAbortWithArgumentError(L"cannot use --target-directory= with --no-target-directory");
			diropt = DirOptionTargetIsDir;
			linkname.emplace(optarg);
			break;
		case 'v':
			verbose = true;
			break;
		}
	}
opts_done:

	if(relative && linktyp != LinkTypeSymbolic) {
		WlnAbortWithArgumentError(L"cannot do --relative without --symbolic");
		return 1;
	}

	std::vector<std::wstring> targets{argv + optind, argv + argc};

	if(targets.empty()) {
		WlnAbortWithArgumentError(L"missing file operand");
		return 1;
	}

	// multiple remaining filenames: one might be the link name.
	if(targets.size() > 1 && !linkname.has_value()) {
		linkname.emplace(std::move(targets.back()));
		targets.resize(targets.size() - 1);
	}

	// still have multiple remaining filenames. if we're linking to a file, no-can-do.
	if(targets.size() > 1 && diropt == DirOptionTargetIsFile) {
		WlnAbortWithArgumentError(L"cannot link multiple targets to a single name");
		return 1;
	}

	if(targets.size() > 1) {
		// more than one target: destination must be a directory
		diropt = DirOptionTargetIsDir;
	}

	if(!linkname.has_value()) {
		linkname = WlnGetFilename(WlnMakePathAbsolute(targets[0]));
	}
	std::wstring finalLinkname{linkname.value()};

	auto linkFi{WlnGetAttributes(finalLinkname)};
	if(linkFi && WlnIsDirectory(linkFi.value())) {
		if(diropt == DirOptionTargetIsFile && WlnIsPhysicalDirectory(linkFi.value())) {
			// only physical directories (unremoveable even with force) will fail -T
			WlnAbortWithReason(L"destination `%ls' is a directory, but --no-target-directory was specified", finalLinkname.c_str());
			return 1;
		}
	} else {
		if(diropt == DirOptionTargetIsDir) {
			WlnAbortWithReason(L"destination `%ls' is not a directory", finalLinkname.c_str());
			return 1;
		}
		// missing file/file existing is okay here; force is processed later
	}

	for(const auto& target : targets) {
		WlnCreateLink(linktyp, diropt, target, finalLinkname, force, relative, verbose, linkFi);
	}

	return 0;
}

static void WlnCreateSymbolicLink(std::wstring target, const std::wstring& link, bool relative) {
	auto targetFi{WlnGetAttributes(target)};
	auto isDir = WlnIsDirectory(targetFi);
	if(relative) {
		std::wstring tabs = WlnMakePathAbsolute(target);
		std::wstring lbase = WlnMakePathAbsoluteAsDirectory(link);
		target = WlnMakePathRelative(tabs, lbase, isDir);
	}

	int flags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
	if(isDir) {
		flags |= SYMBOLIC_LINK_FLAG_DIRECTORY;
	}

	if(!CreateSymbolicLinkW(link.c_str(), target.c_str(), flags)) {
		WlnAbortWithWin32Error(GetLastError(), nullptr);
	}
}

static void WlnCreateJunction(const std::wstring& target, const std::wstring& link) {
	auto targetFi{WlnGetAttributes(target)};
	if(!WlnIsPhysicalDirectory(targetFi)) {
		WlnAbortWithReason(L"`%ls' is not a physical directory", target.c_str());
	}

	std::wstring tabs = WlnMakePathAbsolute(target);
	if(tabs.compare(0, 4, L"\\\\?\\") == 0) {
		tabs[1] = L'?'; // Replace "\\?\" with "\??\"
	}

	if(tabs.compare(0, 4, L"\\??\\") != 0) {
		tabs = L"\\??\\" + tabs;
	}

	if(!CreateDirectoryW(link.c_str(), nullptr)) {
		WlnAbortWithWin32Error(GetLastError(), L"Failed to create junction `%ls'.", link.c_str());
	}

	HANDLE hFile = INVALID_HANDLE_VALUE;
	if(INVALID_HANDLE_VALUE == (hFile = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr))) {
		WlnAbortWithWin32Error(GetLastError(), L"Failed to open junction `%ls'.", link.c_str());
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
		WlnAbortWithWin32Error(GetLastError(), L"Failed to populate reparse point at `%ls'.", link.c_str());
	}
	CloseHandle(hFile);
}

static void WlnCreateLink(LinkType type, DirOption diropt, const std::wstring& target, std::wstring link, bool force, bool relative, bool verbose, const std::optional<WIN32_FILE_ATTRIBUTE_DATA>& linkFileInfo) {
	if(linkFileInfo && diropt != DirOptionTargetIsFile && WlnIsDirectory(linkFileInfo.value())) {
		link += L"\\" + WlnGetFilename(WlnMakePathAbsolute(target));
	}
	link = WlnMakePathAbsolute(link);

	auto destFi{WlnGetAttributes(link)};
	if(destFi) {
		if(WlnIsSameFile(WlnGetFileID(target), WlnGetFileID(link))) {
			WlnAbortWithReason(L"`%ls' and `%ls' are the same file", target.c_str(), link.c_str());
			return;
		}

		if(WlnIsPhysicalDirectory(destFi.value())) {
			WlnAbortWithReason(L"cannot overwrite directory `%ls'", link.c_str());
			return;
		}

		if(!force) {
			WlnAbortWithReason(L"`%ls': destination exists", link.c_str());
		}

		if(WlnIsDirectory(destFi.value())) {
			// we'll only get here if we're looking at a directory symlink/junction
			RemoveDirectoryW(link.c_str());
		} else {
			DeleteFileW(link.c_str());
		}
	}

	if(verbose) {
		fwprintf(stderr, L"`%ls' -> `%ls'\r\n", link.c_str(), target.c_str());
	}

	switch(type) {
	case LinkTypeHard:
		if(!CreateHardLinkW(link.c_str(), target.c_str(), nullptr)) {
			WlnAbortWithWin32Error(GetLastError(), nullptr);
		}
		break;
	case LinkTypeSymbolic:
		WlnCreateSymbolicLink(target, link, relative);
		break;
	case LinkTypeJunction:
		WlnCreateJunction(target, link);
		break;
	}
}