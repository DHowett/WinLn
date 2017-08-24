#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdio.h>

static void usage(wchar_t* pname) {
	fwprintf(stderr, L"Usage: %ls [option] TARGET LINK_NAME\r\n", pname);
}

template <typename... Args>
static void argerr(wchar_t* const pname, const wchar_t* err, Args&&... args) {
	fwprintf(stderr, L"%ls: ", pname);
	fwprintf(stderr, err, args...);
	fwprintf(stderr, L"\r\nTry `%ls --help' for more information.\r\n", pname);
}

template <typename... Args>
static int exit_last_error(const wchar_t* fmt, Args&&... args) {
	wchar_t* buf;
	auto gle = GetLastError();
	FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, nullptr, gle, 0, (LPWSTR)&buf, 1024, nullptr);
	fwprintf(stderr, L"While ");
	fwprintf(stderr, fmt, args...);
	fwprintf(stderr, L", encountered error %8.08x: %ls\r\n", gle, buf);
	HeapFree(GetProcessHeap(), 0, buf);
	return 1;
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
	{L"symlink", L's', false},
	{L"junction", L'j', false},
	{L"help", L'h', false},
	//{L"no-target-directory", L'T', false},
	{nullptr, 0, false},
};

int wmain(int argc, wchar_t** argv) {
	bool force = false, symlink = false, junction = false;
	int optind = 0;
	while(int o = getopt_long(argc, argv, opts, &optind, nullptr)) {
		switch(o) {
		case -1:
			goto opts_done;
		case 'f':
			force = true;
			break;
		case 's':
			symlink = true;
			break;
		case 'j':
			junction = true;
			break;
		case 'h':
			usage(argv[0]);
			return 0;
		}
	}
opts_done:

	if(symlink && junction) {
		argerr(argv[0], L"cannot create symbolic links and junctions at the same time");
		return 1;
	}

	if(optind + 2 > argc) {
		argerr(argv[0], L"missing file operand");
		return 1;
	}

	wchar_t* target = argv[optind];
	wchar_t* linkname = argv[optind + 1];

	WIN32_FILE_ATTRIBUTE_DATA targetFi{};
	if(!GetFileAttributesExW(target, GetFileExInfoStandard, &targetFi)) {
		return exit_last_error(L"statting %ls", target);
	}

	WIN32_FILE_ATTRIBUTE_DATA linkFi{};
	if(int r = GetFileAttributesExW(linkname, GetFileExInfoStandard, &linkFi)) {
		if(!r && GetLastError() != ERROR_NOT_FOUND) return exit_last_error(L"statting %ls", linkname);
		else if(r && force) {
			if(!DeleteFileW(linkname)) {
				return exit_last_error(L"deleting %ls", linkname);
			}
		}
	}

	if(symlink) {
		int flags = 0;
		if((targetFi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
			flags |= SYMBOLIC_LINK_FLAG_DIRECTORY;
		}

		if(!CreateSymbolicLinkW(linkname, target, flags | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
			return exit_last_error(L"creating the symlink");
		}
	} else if(junction) {
		// 
	} else {
		if(!CreateHardLinkW(linkname, target, nullptr)) {
			return exit_last_error(L"creating the hard link");
		}
	}

	return 0;
}

