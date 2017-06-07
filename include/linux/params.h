#ifndef _LINUX_PARAMS_H
#define _LINUX_PARAMS_H

#define NUM_VA_ARGS_IMPL(__dummy, \
			 __1, \
			 __2, \
			 __3, \
			 __4, \
			 __5, \
			 __6, \
			 __7, \
			 __8, \
			 __9, \
			 __10, \
			 __11, \
			 __12, \
			 __13, \
			 __14, \
			 __15, \
			 __16, \
			 __nargs, ...) __nargs

#define NUM_VA_ARGS(...) NUM_VA_ARGS_IMPL(__dummy, ##__VA_ARGS__, \
					  16, \
					  15, \
					  14, \
					  13, \
					  12, \
					  11, \
					  10, \
					  9, \
					  8, \
					  7, \
					  6, \
					  5, \
					  4, \
					  3, \
					  2, \
					  1, \
					  0)

#define PARAM_LIST_N(nargs, ...) PARAM_LIST_##nargs(__VA_ARGS__)
#define PARAM_LIST_FROM_TYPES_N(nargs, ...) PARAM_LIST_N(nargs, __VA_ARGS__)
#define PARAM_LIST_FROM_TYPES(...) \
		PARAM_LIST_FROM_TYPES_N(NUM_VA_ARGS(__VA_ARGS__), __VA_ARGS__)

#define PARAM_LIST_0(type, ...)
#define PARAM_LIST_1(type, ...), type arg0
#define PARAM_LIST_2(type, ...), type arg1 PARAM_LIST_1(__VA_ARGS__)
#define PARAM_LIST_3(type, ...), type arg2 PARAM_LIST_2(__VA_ARGS__)
#define PARAM_LIST_4(type, ...), type arg3 PARAM_LIST_3(__VA_ARGS__)
#define PARAM_LIST_5(type, ...), type arg4 PARAM_LIST_4(__VA_ARGS__)
#define PARAM_LIST_6(type, ...), type arg5 PARAM_LIST_5(__VA_ARGS__)
#define PARAM_LIST_7(type, ...), type arg6 PARAM_LIST_6(__VA_ARGS__)
#define PARAM_LIST_8(type, ...), type arg7 PARAM_LIST_7(__VA_ARGS__)
#define PARAM_LIST_9(type, ...), type arg8 PARAM_LIST_8(__VA_ARGS__)
#define PARAM_LIST_10(type, ...), type arg9 PARAM_LIST_9(__VA_ARGS__)
#define PARAM_LIST_11(type, ...), type arg10 PARAM_LIST_10(__VA_ARGS__)
#define PARAM_LIST_12(type, ...), type arg11 PARAM_LIST_11(__VA_ARGS__)
#define PARAM_LIST_13(type, ...), type arg12 PARAM_LIST_12(__VA_ARGS__)
#define PARAM_LIST_14(type, ...), type arg13 PARAM_LIST_13(__VA_ARGS__)
#define PARAM_LIST_15(type, ...), type arg14 PARAM_LIST_14(__VA_ARGS__)
#define PARAM_LIST_16(type, ...), type arg15 PARAM_LIST_15(__VA_ARGS__)
#define PARAM_LIST_17(type, ...) BUILD_BUG()

#define ARG_NAMES_N(nargs, ...) ARG_NAMES_##nargs(__VA_ARGS__)
#define ARG_NAMES_FROM_TYPES_N(nargs, ...) ARG_NAMES_N(nargs, ...)
#define ARG_NAMES_FROM_TYPES(...) \
		ARG_NAMES_FROM_TYPES_N(NUM_VA_ARGS(__VA_ARGS__), __VA_ARGS__)

#define ARG_NAMES_0(type, ...)
#define ARG_NAMES_1(type, ...) arg0
#define ARG_NAMES_2(type, ...) arg1, ARG_NAMES_1(__VA_ARGS__)
#define ARG_NAMES_3(type, ...) arg2, ARG_NAMES_2(__VA_ARGS__)
#define ARG_NAMES_4(type, ...) arg3, ARG_NAMES_3(__VA_ARGS__)
#define ARG_NAMES_5(type, ...) arg4, ARG_NAMES_4(__VA_ARGS__)
#define ARG_NAMES_6(type, ...) arg5, ARG_NAMES_5(__VA_ARGS__)
#define ARG_NAMES_7(type, ...) arg6, ARG_NAMES_6(__VA_ARGS__)
#define ARG_NAMES_8(type, ...) arg7, ARG_NAMES_7(__VA_ARGS__)
#define ARG_NAMES_9(type, ...) arg8, ARG_NAMES_8(__VA_ARGS__)
#define ARG_NAMES_10(type, ...) arg9, ARG_NAMES_9(__VA_ARGS__)
#define ARG_NAMES_11(type, ...) arg10, ARG_NAMES_10(__VA_ARGS__)
#define ARG_NAMES_12(type, ...) arg11, ARG_NAMES_11(__VA_ARGS__)
#define ARG_NAMES_13(type, ...) arg12, ARG_NAMES_12(__VA_ARGS__)
#define ARG_NAMES_14(type, ...) arg13, ARG_NAMES_13(__VA_ARGS__)
#define ARG_NAMES_15(type, ...) arg14, ARG_NAMES_14(__VA_ARGS__)
#define ARG_NAMES_16(type, ...) arg15, ARG_NAMES_15(__VA_ARGS__)
#define ARG_NAMES_17(type, ...) BUILD_BUG()

#define MATCHER_PARAM_LIST_N(nargs, ...) MATCHER_PARAM_LIST_##nargs(__VA_ARGS__)
#define MATCHER_PARAM_LIST_FROM_TYPES_N(nargs, ...) \
		MATCHER_PARAM_LIST_N(nargs, __VA_ARGS__)
#define MATCHER_PARAM_LIST_FROM_TYPES(...) \
		MATCHER_PARAM_LIST_FROM_TYPES_N(NUM_VA_ARGS(__VA_ARGS__), __VA_ARGS__)

#define MATCHER_PARAM_LIST_0(type, ...)
#define MATCHER_PARAM_LIST_1(type, ...), struct mock_param_matcher *arg0
#define MATCHER_PARAM_LIST_2(type, ...), struct mock_param_matcher *arg1 MATCHER_PARAM_LIST_1(__VA_ARGS__)
#define MATCHER_PARAM_LIST_3(type, ...), struct mock_param_matcher *arg2 MATCHER_PARAM_LIST_2(__VA_ARGS__)
#define MATCHER_PARAM_LIST_4(type, ...), struct mock_param_matcher *arg3 MATCHER_PARAM_LIST_3(__VA_ARGS__)
#define MATCHER_PARAM_LIST_5(type, ...), struct mock_param_matcher *arg4 MATCHER_PARAM_LIST_4(__VA_ARGS__)
#define MATCHER_PARAM_LIST_6(type, ...), struct mock_param_matcher *arg5 MATCHER_PARAM_LIST_5(__VA_ARGS__)
#define MATCHER_PARAM_LIST_7(type, ...), struct mock_param_matcher *arg6 MATCHER_PARAM_LIST_6(__VA_ARGS__)
#define MATCHER_PARAM_LIST_8(type, ...), struct mock_param_matcher *arg7 MATCHER_PARAM_LIST_7(__VA_ARGS__)
#define MATCHER_PARAM_LIST_9(type, ...), struct mock_param_matcher *arg8 MATCHER_PARAM_LIST_8(__VA_ARGS__)
#define MATCHER_PARAM_LIST_10(type, ...), struct mock_param_matcher *arg9 MATCHER_PARAM_LIST_9(__VA_ARGS__)
#define MATCHER_PARAM_LIST_11(type, ...), struct mock_param_matcher *arg10 MATCHER_PARAM_LIST_10(__VA_ARGS__)
#define MATCHER_PARAM_LIST_12(type, ...), struct mock_param_matcher *arg11 MATCHER_PARAM_LIST_11(__VA_ARGS__)
#define MATCHER_PARAM_LIST_13(type, ...), struct mock_param_matcher *arg12 MATCHER_PARAM_LIST_12(__VA_ARGS__)
#define MATCHER_PARAM_LIST_14(type, ...), struct mock_param_matcher *arg13 MATCHER_PARAM_LIST_13(__VA_ARGS__)
#define MATCHER_PARAM_LIST_15(type, ...), struct mock_param_matcher *arg14 MATCHER_PARAM_LIST_14(__VA_ARGS__)
#define MATCHER_PARAM_LIST_16(type, ...), struct mock_param_matcher *arg15 MATCHER_PARAM_LIST_15(__VA_ARGS__)
#define MATCHER_PARAM_LIST_17(type, ...) BUILD_BUG()

#define PTR_TO_ARG_N(nargs, ...) PTR_TO_ARG_##nargs(__VA_ARGS__)
#define PTR_TO_ARG_FROM_TYPES_N(nargs, ...) PTR_TO_ARG_N(nargs, ...)
#define PTR_TO_ARG_FROM_TYPES(...) \
		PTR_TO_ARG_FROM_TYPES_N(NUM_VA_ARGS(__VA_ARGS__), __VA_ARGS__)

#define PTR_TO_ARG_0(type, ...)
#define PTR_TO_ARG_1(type, ...) &arg0
#define PTR_TO_ARG_2(type, ...) &arg1, PTR_TO_ARG_1(__VA_ARGS__)
#define PTR_TO_ARG_3(type, ...) &arg2, PTR_TO_ARG_2(__VA_ARGS__)
#define PTR_TO_ARG_4(type, ...) &arg3, PTR_TO_ARG_3(__VA_ARGS__)
#define PTR_TO_ARG_5(type, ...) &arg4, PTR_TO_ARG_4(__VA_ARGS__)
#define PTR_TO_ARG_6(type, ...) &arg5, PTR_TO_ARG_5(__VA_ARGS__)
#define PTR_TO_ARG_7(type, ...) &arg6, PTR_TO_ARG_6(__VA_ARGS__)
#define PTR_TO_ARG_8(type, ...) &arg7, PTR_TO_ARG_7(__VA_ARGS__)
#define PTR_TO_ARG_9(type, ...) &arg8, PTR_TO_ARG_8(__VA_ARGS__)
#define PTR_TO_ARG_10(type, ...) &arg9, PTR_TO_ARG_9(__VA_ARGS__)
#define PTR_TO_ARG_11(type, ...) &arg10, PTR_TO_ARG_10(__VA_ARGS__)
#define PTR_TO_ARG_12(type, ...) &arg11, PTR_TO_ARG_11(__VA_ARGS__)
#define PTR_TO_ARG_13(type, ...) &arg12, PTR_TO_ARG_12(__VA_ARGS__)
#define PTR_TO_ARG_14(type, ...) &arg13, PTR_TO_ARG_13(__VA_ARGS__)
#define PTR_TO_ARG_15(type, ...) &arg14, PTR_TO_ARG_14(__VA_ARGS__)
#define PTR_TO_ARG_16(type, ...) &arg15, PTR_TO_ARG_15(__VA_ARGS__)
#define PTR_TO_ARG_17(type, ...) BUILD_BUG()

#endif /* _LINUX_PARAMS_H */
