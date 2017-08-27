#include <gtest/gtest.h>
#include <getopt/getopt.h>
#include <type_traits>
#include <vector>
#include <string>

static option opts[] = {
	{L"short", L's', false},
	{L"longname_a", L'a', true},
	{L"longname_b", L'b', false},
	{L"longname_c", L'c', false},
};



TEST(Getopt, ShortOptions) {
	wchar_t* argv[] = {
		L"ProgramName",
		L"Argument 1",
		L"-s", // short option
		L"Argument 2",
		L"-a", // short option that has following parameter
		L"param1",
		L"Argument 3",
		L"-aparam2", // short option that has inline parameter
		L"-bc", // Pack
		L"-bcaparam3", // pack with inline parameter
		L"-bca", // pack with following parameter
		L"param4",
		L"Argument 4",
		L"--", // end of options
		L"-bcaparam5",
		L"Argument 5",
	};

	std::vector<int> responses;
	std::vector<std::wstring> arguments;

	int o = 0;
	_optreset = true;
	while((o = getopt_long(std::extent<decltype(argv)>::value, argv, opts)) != -1) {
		responses.emplace_back(o);
		if(optarg) arguments.emplace_back(optarg);
	}
	const std::vector<std::wstring> afterOptind(argv + optind, argv + std::extent<decltype(argv)>::value);

	std::vector<int> expectedResponses{'s', 'a', 'a', 'b', 'c', 'b', 'c', 'a', 'b', 'c', 'a',};
	std::vector<std::wstring> expectedArguments{
		L"param1",
		L"param2",
		L"param3",
		L"param4",
	};
	std::vector<std::wstring> expectedAfterOptind{
		L"Argument 1",
		L"Argument 2",
		L"Argument 3",
		L"Argument 4",
		L"-bcaparam5",
		L"Argument 5",
	};

	ASSERT_EQ(expectedResponses, responses);
	EXPECT_EQ(expectedArguments, arguments);
	EXPECT_EQ(expectedAfterOptind, afterOptind);
}