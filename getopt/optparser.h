#pragma once

struct option {
	const wchar_t* name;
	wchar_t shopt;
	bool has_arg;
};

#ifdef __cplusplus
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
	void reset(int argc, wchar_t** argv, const option opts[]);
	int next();

	int get_index() const {
		return _optind;
	}

	wchar_t* get_arg() {
		return _optarg;
	}
};
#endif