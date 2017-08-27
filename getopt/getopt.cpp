#include "getopt.h"

#include <cstdlib>
#include <cstring>

class optparser {
private:
	int _argc;
	wchar_t** _argv;

	int _lastarg; // last found arg (for compaction)
	// we have to keep track of lastarg because we must defer compaction
	// until the end of all options in a single pack, but optind must stay
	// the same during pack processing.
	int _optind; // index of current option in argv
	int _optpos; // position of current option in argv[optind] (for packs)
	wchar_t* _optarg; // current argument (if any?)

	const option* _options;

public:
	void reset(int argc, wchar_t** argv, const option opts[]) {
		_argc = argc;
		_argv = argv;
		_lastarg = 1;
		_optind = 1; // skip argv[0], progname
		_optpos = 0;
		_optarg = nullptr;

		_options = &opts[0];
	}

	int next() {
		int idx = _optind;
		if(!_optpos) {
			// no option position (later, if we hit the end of the pack, we clear it)
			for(; idx < _argc && _argv[idx][0] != L'-'; ++idx);
		}

		if(idx == _argc) {
			_optarg = nullptr;
			return -1; // no more args, we're done!
		}

		wchar_t* arg = _argv[idx];
		bool longopt = arg[1] == L'-';

		const option* foundopt = nullptr;
		wchar_t* foundarg = nullptr;
		int npos = 1; // number of positions occupied by args and options

		if(!_optpos && !longopt) _optpos = 1;

		for(const option* o = _options; o->name; ++o) {
			bool found = false;
			if(!longopt && arg[_optpos] == o->shopt) {
				found = true;
				if(o->has_arg && arg[_optpos + 1] != L'\0') {
					// special case: argument specified right after option
					foundarg = arg + _optpos + 1;
					_optpos = -1; // incremented later, to 0
				}
			} else if(longopt) {
				wchar_t* eq = wcschr(arg, L'=');
				int arglen = eq ? eq - arg - 2 : wcslen(arg + 2); // 2: length of --
				if(
					wcslen(o->name) != arglen // lengths don't match
					|| wcsncmp(arg + 2, o->name, arglen) != 0 // or text doesn't match
					) continue; // no match

				if(eq) {
					foundarg = eq + 1;
				}
				found = true;
			} else {
				continue; // no match
			}

			if(found) {
				if(o->has_arg && !foundarg) {
					int argpos = idx + 1;
					if(argpos >= _argc) break; // option missing arg: opterr?

					++npos; // Consume another slot
					foundarg = _argv[argpos];
				}

				foundopt = o;
				break;
			}
		}

		_optpos += static_cast<int>(!longopt); // increment only if short opt
		_optind = idx;

		if(!_optpos || arg[_optpos] == L'\0') {
			_optpos = 0;
			if(idx != _lastarg) {
				// if this option isn't adjacent to the last one,
				// move it down so it is.

				wchar_t* save[2];
				memmove(save, _argv + idx, npos * sizeof(wchar_t*));
				// 12 34 -s 56 78 -r val
				// |la   |idx         npos=1
				// |S |D              (memmove Src, Dst)
				// |-----|            (memmove Len)
				// later,
				// -s 12 34 56 78 -r val
				//    |la         |idx npos=2
				//    |S    | D        Src, Dst
				//    |-----------|    Len
				memmove(_argv + _lastarg + npos, _argv + _lastarg, (idx - _lastarg) * sizeof(wchar_t*));
				memmove(_argv + _lastarg, save, npos * sizeof(wchar_t*));
			}
			_lastarg += npos;
			_optind = _lastarg;
		}

		_optarg = foundarg;

		if(longopt && arg[2] == L'\0') {
			return -1;
		}

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

#ifdef TEST_GETOPT
#include <cstdio>
int wmain(int argc, wchar_t** argv) {
	static option opts[]{
		{L"a_first", L'a', false},
		{L"b_second", L'b', false},
		{L"c_witharg", L'c', true},
		{nullptr, 0, false},
	};

	int optind = 0;
	wchar_t* optarg = nullptr;
	while(int o = getopt_long(argc, argv, opts, &optind, &optarg)) {
		if(o == -1) {
			fwprintf(stderr, L"Done with argument parsing.\r\n");
			break;
		}
		fwprintf(stderr, L"Got option `%c' at index %d with argument `%ls'\r\n", o, optind, optarg);
	}

	fwprintf(stderr, L"\r\nArguments (* = after optind)\r\n----\r\n");
	for(int i = 0; i < argc; ++i) {
		fwprintf(stderr, L"%c %ls\r\n", i >= optind ? L'*' : L' ', argv[i]);
	}

	return 0;
}
#endif
