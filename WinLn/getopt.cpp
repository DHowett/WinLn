#include "getopt.h"

#include <cstdlib>
#include <cstring>

int getopt_long(int argc, wchar_t** argv, const option opts[], int* idxptr, wchar_t** optargptr) {
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
