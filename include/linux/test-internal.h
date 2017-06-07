#ifndef _LINUX_TEST_INTERNAL_H
#define _LINUX_TEST_INTERNAL_H

#include <linux/types.h>
#include <linux/slab.h>

struct test_resource {
	struct list_head node;
	void *allocation;
};

struct test;

struct test_case {
	void (*run_case)(struct test *test);
	const char name[256];
	bool success;
};

#define TEST_CASE(test_name) { .run_case = test_name, .name = #test_name }

struct test_module {
	const char name[256];
	int (*init)(struct test *test);
	void (*exit)(struct test *test);
	struct test_case *test_cases;
};

struct test {
	struct list_head resources;
	struct test_module *module;
	void *priv;
	bool success;
};

int test_run_tests(struct test_module *module);

#define module_test(module) \
		static int module_test_init(void) \
		{ \
			return test_run_tests(&module); \
		} \
		late_initcall(module_test_init)

void *test_kmalloc(struct test *test, size_t size, gfp_t gfp);

static inline void *test_kzalloc(struct test *test, size_t size, gfp_t gfp)
{
	return test_kmalloc(test, size, gfp | __GFP_ZERO);
}

void test_cleanup(struct test *test);

void test_printk(const char *level, const struct test *test, const char *fmt, ...);

#define test_info(test, fmt, ...) \
		test_printk(KERN_INFO, test, fmt, ##__VA_ARGS__)

static inline void test_expect(struct test *test, bool success)
{
	if (!success)
		test->success = false;
}

#define EXPECT(test, condition) test_expect(test, condition)
#define EXPECT_EQ(test, expected, actual) EXPECT(test, expected == actual)

#endif /* _LINUX_TEST_INTERNAL_H */
