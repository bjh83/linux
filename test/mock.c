#include <linux/mock.h>

bool mock_match_params(struct mock_matcher *matcher, const void **params, int len)
{
	struct mock_param_matcher *param_matcher;
	int i;

	BUG_ON(matcher->num != len);

	for (i = 0; i < matcher->num; i++) {
		param_matcher = matcher->matchers[i];
		if (!param_matcher->match(param_matcher, params[i]))
			return false;
	}

	return true;
}

const void *mock_do_expect(struct mock *mock, const void *method_ptr, void **params, int len);

void mock_init(struct test *test, struct mock *mock)
{
	mock->test = test;
	INIT_LIST_HEAD(&mock->methods);
	mock->do_expect = mock_do_expect;
}

void mock_validate_expectations(struct mock *mock)
{
	struct mock_expectation *expectation;
	struct mock_method *method;
	int times_called;

	list_for_each_entry(method, &mock->methods, node) {
		list_for_each_entry(expectation, &method->expectations, node) {
			times_called = expectation->times_called;
			if (!(expectation->max_calls_expected <= times_called &&
			      times_called <= expectation->min_calls_expected))
				mock->test->success = false;
		}
	}
}

static struct mock_method *mock_lookup_method(struct mock *mock, const void *method_ptr)
{
	struct mock_method *ret;

	list_for_each_entry(ret, &mock->methods, node) {
		if (ret->method_ptr == method_ptr)
			return ret;
	}

	return NULL;
}

static struct mock_method *mock_add_method(struct mock *mock, const void *method_ptr)
{
	struct mock_method *method;

	method = test_kzalloc(mock->test, sizeof(*method), GFP_KERNEL);
	if (!method)
		return NULL;

	INIT_LIST_HEAD(&method->expectations);
	method->method_ptr = method_ptr;
	list_add_tail(&method->node, &mock->methods);

	return method;
}

static int mock_add_expectation(struct mock *mock, const void *method_ptr,
				struct mock_expectation *expectation)
{
	struct mock_method *method;

	method = mock_lookup_method(mock, method_ptr);
	if (!method) {
		method = mock_add_method(mock, method_ptr);
		if (!method)
			return -ENOMEM;
	}

	list_add_tail(&expectation->node, &method->expectations);

	return 0;
}

struct mock_expectation *mock_add_matcher(struct mock *mock,
					  const void *method_ptr,
					  struct mock_param_matcher *matchers[],
					  int len)
{
	struct mock_expectation *expectation;
	struct mock_matcher *matcher;
	int ret;

	expectation = test_kzalloc(mock->test, sizeof(*expectation), GFP_KERNEL);
	if (!expectation)
		return NULL;

	matcher = test_kmalloc(mock->test, sizeof(*matcher), GFP_KERNEL);
	if (!matcher)
		return NULL;

	memcpy(&matcher->matchers, matchers, sizeof(*matchers) * len);
	matcher->num = len;

	expectation->matcher = matcher;
	expectation->max_calls_expected = 1;
	expectation->min_calls_expected = 1;

	ret = mock_add_expectation(mock, method_ptr, expectation);
	if (ret < 0)
		return NULL;

	return expectation;
}

int mock_set_default_action(struct mock *mock, const void *method_ptr, struct mock_action *action)
{
	struct mock_method *method;

	method = mock_lookup_method(mock, method_ptr);
	if (!method) {
		method = mock_add_method(mock, method_ptr);
		if (!method)
			return -ENOMEM;
	}

	method->default_action = action;

	return 0;
}

struct mock_expectation *mock_apply_expectations(struct mock_method *method, const void **params, int len)
{
	struct mock_expectation *ret;

	list_for_each_entry(ret, &method->expectations, node) {
		if (mock_match_params(ret->matcher, params, len))
			return ret;
	}

	return NULL;
}

const void *mock_do_expect(struct mock *mock, const void *method_ptr, void **params, int len)
{
	struct mock_expectation *expectation;
	struct mock_method *method;
	struct mock_action *action;

	method = mock_lookup_method(mock, method_ptr);
	if (!method)
		return NULL;

	expectation = mock_apply_expectations(method, (const void **) params, len);
	if (!expectation) {
		action = method->default_action;
	} else {
		expectation->times_called++;
		if (expectation->action)
			action = expectation->action;
		else
			action = method->default_action;
	}
	if (!action)
		return NULL;

	return action->do_action(action, params, len);
}
