#pragma once

struct option {
	const wchar_t* name;
	wchar_t shopt;
	bool has_arg;
};

extern bool _optreset;
int getopt_long(int argc, wchar_t** argv, const struct option opts[], int* idxptr, wchar_t** optargptr);