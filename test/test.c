#include <linux/test-internal.h>

int test_run_tests(struct test_module *module)
{
	struct test_case *test_case;
	struct test test;
	bool all_passed = true;
	int ret;

	INIT_LIST_HEAD(&test.resources);
	test.module = module;

	for (test_case = module->test_cases; test_case->run_case; test_case++) {
		test.success = true;

		ret = module->init(&test);
		if (ret < 0) {
			test_info(&test, "failed to initialize: %d", ret);
			return ret;
		}

		test_case->run_case(&test);
		test_info(&test, "%s %s", test_case->name, test.success ? "passed" : "failed");

		if (!test.success)
			all_passed = false;

		module->exit(&test);

		test_cleanup(&test);
	}

	if (all_passed)
		test_info(&test, "all tests passed");
	else
		test_info(&test, "one or more tests failed");

	return 0;
}

void *test_kmalloc(struct test *test, size_t size, gfp_t gfp)
{
	struct test_resource *res;
	void *ret;

	res = kmalloc(sizeof(*res), GFP_KERNEL);
	if (!res)
		return ret;

	ret = kmalloc(size, gfp);
	if (!ret) {
		kfree(res);
		return ret;
	}

	res->allocation = ret;
	list_add_tail(&res->node, &test->resources);

	return ret;
}

void test_cleanup(struct test *test)
{
	struct test_resource *resource, *resource_safe;

	list_for_each_entry_safe(resource, resource_safe, &test->resources, node) {
		kfree(resource->allocation);
		list_del(&resource->node);
		kfree(resource);
	}
}

void test_printk(const char *level, const struct test *test, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	printk("%sktest %s: %pV", level, test->module->name, &vaf);

	va_end(args);
}
