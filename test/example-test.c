#include <linux/test.h>
#include <linux/mock.h>

struct example {
	int (*foo)(struct example *example, int num);
};

struct example_mock {
	struct mock mock;
	struct example example;
};

struct mock *from_example_to_mock(struct example *example)
{
	struct example_mock *mock = container_of(example, struct example_mock, example);

	return &mock->mock;
}

DEFINE_MOCK(foo, example, int, int);

static void example_simple_test(struct test *test)
{
	EXPECT_EQ(test, 1, 1);
}

static void example_mock_test(struct test *test) {
	struct example_mock *mock = test->priv;
	struct mock_expectation *handle;

	handle = EXPECT_CALL(foo(&mock->mock, int_eq(test, 5)));
	handle->action = int_return(test, 2);

	EXPECT_EQ(test, 2, mock->example.foo(&mock->example, 5));
}

static int example_test_init(struct test *test)
{
	struct example_mock *mock;

	test_info(test, "initializing");

	mock = test_kzalloc(test, sizeof(*mock), GFP_KERNEL);
	if (!mock)
		return -ENOMEM;

	test->priv = mock;
	mock_init(test, &mock->mock);
	mock->example.foo = foo;

	return 0;
}

static void example_test_exit(struct test *test)
{
	struct example_mock *mock = test->priv;

	test_info(test, "exiting");
	mock_validate_expectations(&mock->mock);
}

static struct test_case example_test_cases[] = {
	TEST_CASE(example_simple_test),
	TEST_CASE(example_mock_test),
	{},
};

static struct test_module example_test_module = {
	.name = "example",
	.init = example_test_init,
	.exit = example_test_exit,
	.test_cases = example_test_cases,
};
module_test(example_test_module);
