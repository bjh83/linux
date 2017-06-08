#include <linux/test.h>
#include <linux/i2c-mock.h>
#include <linux/hwmon.h>

struct lm75_test_data {
	struct i2c_mock i2c_mock;
	struct hwmon_device *hwmon_dev;
};

static void lm75_test_read(struct test *test)
{
	struct lm75_test_data *test_data = test->priv;
	struct hwmon_device *hwmon_dev = test_data->hwmon_dev;
	const struct hwmon_ops *ops = hwmon_dev->chip->ops;
	struct mock *mock = &test_data->i2c_mock.mock;
	struct mock_expectation *handle;
	long temp;

	// TODO(brendanhiggins): add matchers for struct i2c_msg and actions to
	// manipulate them.
	handle = EXPECT_CALL(master_xfer(mock, any(test), any(test)));
	handle->action = int_return(test, 2);

	EXPECT_EQ(test, 0, ops->read(&hwmon_dev->dev, hwmon_temp, hwmon_temp_input, 0, &temp));
}

static int lm75_test_device_match(struct device *dev, const void *data)
{
	return dev->parent == data;
}

static int lm75_test_init(struct test *test)
{
	const struct i2c_device_id *i2c_device_id;
	struct lm75_test_data *test_data;
	struct mock_expectation *handle;
	struct i2c_driver *lm75_driver;
	struct i2c_client *client;
	struct device *lm75_dev;
	struct i2c_mock *mock;
	int ret;

	test_data = test_kzalloc(test, sizeof(*test_data), GFP_KERNEL);
	if (!test_data)
		return -ENOMEM;

	test->priv = test_data;
	mock = &test_data->i2c_mock;
	ret = i2c_mock_init(test, mock);
	if (ret < 0)
		return ret;

	client = i2c_new_dummy(&mock->adap, 0x53);
	if (!client)
		return -EINVAL;
	snprintf(client->name, sizeof(client->name), "lm75");

	lm75_driver = i2c_driver_find("lm75");
	if (!lm75_driver)
		return -ENODEV;

	i2c_device_id = i2c_match_id(lm75_driver->id_table, client);
	if (!i2c_device_id)
		return -ENODEV;

	handle = EXPECT_CALL(i2c_smbus_read_byte_data(client, u8_ge(test, 0)));
	handle->action = int_return(test, 1);

	ret = lm75_driver->probe(client, i2c_device_id);
	if (ret < 0)
		return ret;

	lm75_dev = class_find_device(&hwmon_class, NULL, &client->dev, lm75_test_device_match);
	if (!lm75_dev)
		return -ENODEV;

	test_data->hwmon_dev = to_hwmon_device(lm75_dev);

	return 0;
}

static void lm75_test_exit(struct test *test)
{
	struct i2c_mock *i2c_mock = test->priv;

	mock_validate_expectations(&i2c_mock->mock);
}

static struct test_case lm75_test_cases[] = {
	TEST_CASE(lm75_test_read),
	{},
};

static struct test_module lm75_test_module = {
	.name = "lm75",
	.init = lm75_test_init,
	.exit = lm75_test_exit,
	.test_cases = lm75_test_cases,
};
module_test(lm75_test_module);
