#pragma once

#include "optparser.h"

#ifdef __cplusplus
extern "C" {
#endif

extern bool _optreset;
extern int optind;
extern wchar_t* optarg;
int getopt_long(int argc, wchar_t** argv, const struct option opts[]);

#ifdef __cplusplus
}
#endif