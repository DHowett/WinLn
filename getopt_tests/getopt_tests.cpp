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

struct testcase {
	std::vector<wchar_t*> cmdline;
	std::vector<int> expectedResponses;
	std::vector<std::wstring> expectedOptionArguments;
	std::vector<std::wstring> expectedRemainingArguments;
};

class GetoptTest: public ::testing::TestWithParam<struct testcase> {
public:
	void SetUp() {
		auto& testcase = GetParam();
		std::vector<wchar_t*> copiedCmdline{testcase.cmdline};
		std::vector<int> responses;
		std::vector<std::wstring> arguments;
		int o = 0;
		_optreset = true;
		while((o = getopt_long(copiedCmdline.size(), copiedCmdline.data(), opts)) != -1) {
			responses.emplace_back(o);
			if(optarg) arguments.emplace_back(optarg);
		}

		const std::vector<std::wstring> afterOptind(copiedCmdline.begin() + optind, copiedCmdline.end());

		_responses = std::move(responses);
		_arguments = std::move(arguments);
		_remainingParameters = std::move(afterOptind);
	}
protected:
	std::vector<int> _responses;
	std::vector<std::wstring> _arguments;
	std::vector<std::wstring> _remainingParameters;
};

TEST_P(GetoptTest, ReturnsOptions) {
	auto& testcase = GetParam();
	ASSERT_EQ(testcase.expectedResponses, _responses);
}

TEST_P(GetoptTest, ReturnsOptionArguments) {
	auto& testcase = GetParam();
	EXPECT_EQ(testcase.expectedOptionArguments, _arguments);
}

TEST_P(GetoptTest, SortsAndReturnsNonOptionParameters) {
	auto& testcase = GetParam();
	EXPECT_EQ(testcase.expectedRemainingArguments, _remainingParameters);
}

struct testcase cases[]{
	{ // No options
		/* argv */ {L"ProgramName", L"Argument 1"},
		/* resp */ {},
		/* args */ {},
		/* left */ {L"Argument 1"},
	},
	{ // Short Options
		/* argv */ {L"ProgramName", L"Argument 1", L"-s", L"-b", L"-c", L"-a", L"hello", L"Argument 2"},
		/* resp */ {L's', 'b', 'c', 'a'},
		/* args */ {L"hello"},
		/* left */ {L"Argument 1", L"Argument 2"},
	},
	{ // Short Options - Packed
		/* argv */ {L"ProgramName", L"Argument 1", L"-sbc", L"-ahello", L"Argument 2"},
		/* resp */ {L's', 'b', 'c', 'a'},
		/* args */ {L"hello"},
		/* left */ {L"Argument 1", L"Argument 2"},
	},
	{ // Short Options - Packed and Unpacked
		/* argv */ {L"ProgramName", L"Argument 1", L"-s", L"-bc", L"-sbahello", L"Argument 2", L"-s"},
		/* resp */ {L's', 'b', 'c', 's', 'b', 'a', 's'},
		/* args */ {L"hello"},
		/* left */ {L"Argument 1", L"Argument 2"},
	},
	{ // Long Options
		/* argv */ {L"ProgramName", L"Argument 1", L"--short", L"--longname_a=value", L"--longname_a", L"value 2", L"Argument 2", L"--longname_b"},
		/* resp */ {L's', 'a', 'a', 'b'},
		/* args */ {L"value", L"value 2"},
		/* left */ {L"Argument 1", L"Argument 2"},
	},
	{ // Long Options, Interspersed with Short Options
		/* argv */ {L"ProgramName", L"Argument 1", L"--short", L"-avalue", L"--longname_a", L"value 2", L"Argument 2", L"-bc"},
		/* resp */ {L's', 'a', 'a', 'b', 'c'},
		/* args */ {L"value", L"value 2"},
		/* left */ {L"Argument 1", L"Argument 2"},
	},
	{ // -- (before all options)
		/* argv */ {L"ProgramName", L"--", L"Argument 1", L"--short", L"-avalue", L"--longname_a", L"value 2", L"Argument 2", L"-bc"},
		/* resp */ {},
		/* args */ {},
		/* left */ {L"Argument 1", L"--short", L"-avalue", L"--longname_a", L"value 2", L"Argument 2", L"-bc"},
	},
	{ // -- (after some options)
		/* argv */ {L"ProgramName",  L"Argument 1", L"--short", L"-avalue", L"--longname_a", L"value 2", L"--", L"Argument 2", L"-bc"},
		/* resp */ {'s', 'a', 'a'},
		/* args */ {L"value", L"value 2"},
		/* left */ {L"Argument 1", L"Argument 2", L"-bc"},
	},
	{ // bad options
		/* argv */ {L"ProgramName",  L"--fun"},
		/* resp */ {'?'},
		/* args */ {},
		/* left */ {},
	},
};

INSTANTIATE_TEST_CASE_P(Getopt, GetoptTest, ::testing::ValuesIn(cases));