#include "getopt.h"

#include <cstdlib>
#include <cstring>

class optparser {
private:
	int _argc;
	wchar_t** _argv;

	int _optind; // index of current option in argv
	int _optpos; // position of current option in argv[optind] (for packs)
	wchar_t* _optarg; // current argument (if any?)

	const option* _options;

public:
	void reset(int argc, wchar_t** argv, const option opts[]) {
		_argc = argc;
		_argv = argv;
		_optind = 1; // skip argv[0], progname
		_optpos = 0;
		_optarg = nullptr;

		_options = &opts[0];
	}

	int next() {
		int fa = _optind;
		for(; fa < _argc && _argv[fa][0] != L'-'; ++fa);

		if(fa == _argc) {
			return -1; // no more args, we're done!
		}

		// found the first argument.
		wchar_t* arg = _argv[fa];
		const option* foundopt = nullptr;
		wchar_t* foundarg = nullptr;
		int start = 1 + static_cast<int>(arg[1] == L'-');
		wchar_t* eq = start == 2 ? wcschr(arg, L'=') : nullptr;
		int arglen = eq ? eq - arg - start : wcslen(arg) - start;

		for(const option* o = _options; o->name; ++o) {
			//if(o->name == nullptr) {
				//break; // arg not found! opterr?
			//}

			if((start == 1 && (arg[start] != o->shopt || arg[start + 1] != L'\0'))
				|| (start == 2 && wcsncmp(o->name, arg + start, arglen) != 0)) {
				continue; // next arg
			}

			if(start == 2 && eq) {
				if(!o->has_arg) break;
				foundarg = arg + (start + arglen + 1); // account for -- and =
			} else if(o->has_arg) {
				int argpos = fa + 1;
				if(argpos >= _argc) {
					break; // arg missing arg: opterr?
				}
				foundarg = _argv[argpos];
			}
			foundopt = o;
			break;
		}

		// "1 -a 2" -> "-a 2 1"
		// move everything between idx and fa (or fa+1) over end of fa.
		int nnonargs = fa - _optind; // num non args
		memmove(_argv + fa + 1 + static_cast<int>(foundarg && !eq) - nnonargs, _argv + _optind, nnonargs * sizeof(wchar_t*));
		_argv[_optind++] = arg;
		if(foundarg && !eq) {
			_argv[_optind++] = foundarg;
		}

		_optarg = foundarg;
		return foundopt ? foundopt->shopt : L'?';
	}

	int get_index() const {
		return _optind;
	}

	wchar_t* get_arg() {
		return _optarg;
	}
};

static bool _optreset = false;
static optparser globalParser;
int getopt_long(int argc, wchar_t** argv, const option opts[], int* idxptr, wchar_t** optargptr) {
	if(!_optreset) {
		globalParser.reset(argc, argv, opts);
		_optreset = true;
	}

	int ret = globalParser.next();
	if(idxptr) *idxptr = globalParser.get_index();
	if(optargptr) *optargptr = globalParser.get_arg();
	return ret;
}
