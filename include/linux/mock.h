#ifndef _LINUX_MOCK_H
#define _LINUX_MOCK_H

#include <linux/test-internal.h>
#include <linux/params.h>
#include <linux/types.h>

struct mock_param_matcher {
	bool (*match)(struct mock_param_matcher *this, const void *param);
};

#define MOCK_MAX_PARAMS 255

struct mock_matcher {
	struct mock_param_matcher *matchers[MOCK_MAX_PARAMS];
	int num;
};

bool mock_match_params(struct mock_matcher *matcher, const void **params, int len);

struct mock_action {
	void *(*do_action)(struct mock_action *this, void **params, int len);
};

struct mock_expectation {
	struct list_head node;
	struct mock_matcher *matcher;
	struct mock_action *action;
	int times_called;
	int max_calls_expected;
	int min_calls_expected;
};

struct mock_method {
	struct list_head node;
	const void *method_ptr;
	struct mock_action *default_action;
	struct list_head expectations;
};

struct mock {
	struct test *test;
	struct list_head methods;
	const void * (*do_expect)(struct mock *mock, const void *method_ptr, void **params, int len);
};

void mock_init(struct test *test, struct mock *mock);

void mock_validate_expectations(struct mock *mock);

// void mock_lookup_mock(struct mock *mock, const void *method_ptr, struct mock_method **mock_method);

// void mock_register_mock(struct mock *mock, void *method_ptr);

int mock_set_default_action(struct mock *mock, const void *method_ptr, struct mock_action *action);

struct mock_expectation *mock_add_matcher(struct mock *mock,
					  const void *method_ptr,
					  struct mock_param_matcher *matchers[],
					  int len);

#define EXPECT_CALL(method_with_args) mock_master_##method_with_args;

#define DECLARE_MOCK_CLIENT(name, handle_type, return_type, ...) \
		return_type name(struct handle_type *handle PARAM_LIST_FROM_TYPES(__VA_ARGS__));

#define DECLARE_MOCK_MASTER(name, ...) \
		struct mock_expectation *mock_master_##name(struct mock *mock MATCHER_PARAM_LIST_FROM_TYPES(__VA_ARGS__));

#define DECLARE_MOCK(name, handle_type, return_type, ...) \
		DECLARE_MOCK_CLIENT(name, handle_type, return_type, __VA_ARGS__); \
		DECLARE_MOCK_MASTER(name, __VA_ARGS__);

#define DEFINE_MOCK_CLIENT(name, handle_type, return_type, ...) \
		return_type name(struct handle_type *handle PARAM_LIST_FROM_TYPES(__VA_ARGS__)) \
		{ \
			struct mock *mock = from_##handle_type##_to_mock(handle); \
			void *params[] = { PTR_TO_ARG_FROM_TYPES(__VA_ARGS__) }; \
			\
			const void *retval = mock->do_expect(mock, &name, params, ARRAY_SIZE(params)); \
			if (!retval) {\
				test_info(mock->test, "no action installed for "#name); \
				BUG();\
			}\
			return *((return_type *) retval); \
		}

#define DEFINE_MOCK_MASTER(name, ...) \
		struct mock_expectation *mock_master_##name(struct mock *mock MATCHER_PARAM_LIST_FROM_TYPES(__VA_ARGS__)) \
		{ \
			struct mock_param_matcher *matchers[] = { ARG_NAMES_FROM_TYPES(__VA_ARGS__) }; \
			\
			return mock_add_matcher(mock, (const void *) name, matchers, ARRAY_SIZE(matchers)); \
		}

#define DEFINE_MOCK(name, handle_type, return_type, ...) \
		DEFINE_MOCK_CLIENT(name, handle_type, return_type, __VA_ARGS__); \
		DEFINE_MOCK_MASTER(name, __VA_ARGS__);

struct mock_param_matcher *any(struct test *test);

struct mock_param_matcher *int_eq(struct test *test, int expected);
struct mock_param_matcher *int_lt(struct test *test, int expected);
struct mock_param_matcher *int_le(struct test *test, int expected);
struct mock_param_matcher *int_gt(struct test *test, int expected);
struct mock_param_matcher *int_ge(struct test *test, int expected);
struct mock_action *int_return(struct test *test, int ret);

struct mock_param_matcher *u8_eq(struct test *test, u8 expected);
struct mock_param_matcher *u8_ne(struct test *test, u8 expected);
struct mock_param_matcher *u8_le(struct test *test, u8 expected);
struct mock_param_matcher *u8_lt(struct test *test, u8 expected);
struct mock_param_matcher *u8_ge(struct test *test, u8 expected);
struct mock_param_matcher *u8_gt(struct test *test, u8 expected);

struct mock_param_matcher *u16_eq(struct test *test, u16 expected);
struct mock_param_matcher *ushort_eq(struct test *test, unsigned short expected);
struct mock_param_matcher *char_eq(struct test *test, char expected);

#endif /* _LINUX_MOCK_H */
