#include <linux/mock.h>
#include <linux/kernel.h>

static bool match_any(struct mock_param_matcher *pmatcher, const void *actual)
{
	return true;
}

static struct mock_param_matcher any_matcher = {
	.match = match_any,
};

struct mock_param_matcher *any(struct test *test)
{
	return &any_matcher;
}

#define DEFINE_MATCHER_STRUCT(type_name, type) \
		struct mock_##type_name##_matcher { \
			struct mock_param_matcher matcher; \
			type expected; \
		};

#define DEFINE_TO_MATCHER_STRUCT(type_name) \
		struct mock_##type_name##_matcher *to_mock_##type_name##_matcher(struct mock_param_matcher *matcher) \
		{ \
			return container_of(matcher, struct mock_##type_name##_matcher, matcher); \
		}

#define DEFINE_MATCH_FUNC(type_name, type, op_name, op) \
		bool match_##type_name##_##op_name(struct mock_param_matcher *pmatcher, const void *actual) \
		{ \
			struct mock_##type_name##_matcher *matcher = to_mock_##type_name##_matcher(pmatcher); \
			\
			return *((type *) actual) op matcher->expected; \
		}

#define DEFINE_MATCH_FACTORY(type_name, type, op_name) \
		struct mock_param_matcher *type_name##_##op_name(struct test *test, type expected) \
		{\
			struct mock_##type_name##_matcher *matcher; \
			\
			matcher = test_kmalloc(test, sizeof(*matcher), GFP_KERNEL); \
			if (!matcher) \
				return NULL; \
			\
			matcher->matcher.match = match_##type_name##_##op_name; \
			matcher->expected = expected; \
			return &matcher->matcher; \
		}

#define DEFINE_MATCHER_WITH_TYPENAME(type_name, type) \
		DEFINE_MATCHER_STRUCT(type_name, type); \
		DEFINE_TO_MATCHER_STRUCT(type_name); \
		DEFINE_MATCH_FUNC(type_name, type, eq, ==); \
		DEFINE_MATCH_FACTORY(type_name, type, eq); \
		DEFINE_MATCH_FUNC(type_name, type, ne, !=); \
		DEFINE_MATCH_FACTORY(type_name, type, ne); \
		DEFINE_MATCH_FUNC(type_name, type, le, <=); \
		DEFINE_MATCH_FACTORY(type_name, type, le); \
		DEFINE_MATCH_FUNC(type_name, type, lt, <); \
		DEFINE_MATCH_FACTORY(type_name, type, lt); \
		DEFINE_MATCH_FUNC(type_name, type, ge, >=); \
		DEFINE_MATCH_FACTORY(type_name, type, ge); \
		DEFINE_MATCH_FUNC(type_name, type, gt, >); \
		DEFINE_MATCH_FACTORY(type_name, type, gt);

#define DEFINE_MATCHER(type) DEFINE_MATCHER_WITH_TYPENAME(type, type)

DEFINE_MATCHER(u8);
DEFINE_MATCHER(u16);
DEFINE_MATCHER(u32);
DEFINE_MATCHER(u64);
DEFINE_MATCHER(char);
DEFINE_MATCHER_WITH_TYPENAME(uchar, unsigned char);
DEFINE_MATCHER_WITH_TYPENAME(schar, signed char);
DEFINE_MATCHER(short);
DEFINE_MATCHER_WITH_TYPENAME(ushort, unsigned short);
DEFINE_MATCHER(int);
DEFINE_MATCHER_WITH_TYPENAME(uint, unsigned int);
DEFINE_MATCHER(long);
DEFINE_MATCHER_WITH_TYPENAME(ulong, unsigned long);
DEFINE_MATCHER_WITH_TYPENAME(longlong, long long);
DEFINE_MATCHER_WITH_TYPENAME(ulonglong, unsigned long long);

struct mock_int_action {
	struct mock_action action;
	int ret;
};

void *do_int_return(struct mock_action *paction, void **params, int len)
{
	struct mock_int_action *action = container_of(paction, struct mock_int_action, action);

	return (void *) &action->ret;
}

struct mock_action *int_return(struct test *test, int ret)
{
	struct mock_int_action *action;

	action = test_kmalloc(test, sizeof(*action), GFP_KERNEL);
	if (!action)
		return NULL;

	action->action.do_action = do_int_return;
	action->ret = ret;

	return &action->action;
}
