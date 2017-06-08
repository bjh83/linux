#include <linux/mock.h>
#include <linux/i2c.h>

struct i2c_mock {
	struct mock mock;
	struct i2c_adapter adap;
};

static inline struct mock *from_i2c_adapter_to_mock(struct i2c_adapter *adap)
{
	struct i2c_mock *mock = container_of(adap, struct i2c_mock, adap);

	return &mock->mock;
}

DECLARE_MOCK(master_xfer, i2c_adapter, int, struct i2c_msg *, int);
DECLARE_MOCK(smbus_xfer, i2c_adapter, int, u16, unsigned short, char, u8, int, union i2c_smbus_data *);
DECLARE_MOCK(functionality, i2c_adapter, u32);

static inline struct mock_expectation *mock_master_i2c_smbus_read_byte_data(
		struct i2c_client *client, struct mock_param_matcher *u8_matcher)
{
	struct mock *mock = from_i2c_adapter_to_mock(client->adapter);
	struct test *test = mock->test;

	return EXPECT_CALL(smbus_xfer(mock, u16_eq(test, client->addr),
				      ushort_eq(test, client->flags),
				      char_eq(test, I2C_SMBUS_READ),
				      u8_matcher,
				      int_eq(test, I2C_SMBUS_BYTE_DATA),
				      any(test)));
}

int i2c_mock_init(struct test *test, struct i2c_mock *i2c_mock);

static inline struct i2c_driver *i2c_driver_find(const char *name)
{
	struct device_driver *driver;

	driver = driver_find(name, &i2c_bus_type);
	if (!driver)
		return NULL;

	return to_i2c_driver(driver);
}
