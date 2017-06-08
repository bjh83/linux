#include <linux/mock.h>
#include <linux/i2c.h>
#include <linux/i2c-mock.h>

DEFINE_MOCK(master_xfer, i2c_adapter, int, struct i2c_msg *, int);
DEFINE_MOCK(smbus_xfer, i2c_adapter, int, u16, unsigned short, char, u8, int, union i2c_smbus_data *);
DEFINE_MOCK(functionality, i2c_adapter, u32);

static const struct i2c_algorithm i2c_mock_algorithm = {
	.master_xfer = master_xfer,
	.smbus_xfer = smbus_xfer,
	.functionality = functionality,
};

static int i2c_mock_num_vf(struct device *dev)
{
	return 1;
}

static struct bus_type i2c_mock_bus = {
	.name = "i2c_mock_bus",
	.num_vf = i2c_mock_num_vf,
};

static void i2c_mock_release(struct device *dev)
{
}

struct device i2c_mock_device  = {
	.init_name = "i2c_mock_device",
	.bus = &i2c_mock_bus,
	.release = i2c_mock_release,
};

int i2c_mock_init(struct test *test, struct i2c_mock *i2c_mock)
{
	struct i2c_adapter *adap = &i2c_mock->adap;
	struct mock *mock = &i2c_mock->mock;
	int ret;

	ret = bus_register(&i2c_mock_bus);
	if (ret < 0)
		return ret;

	ret = device_register(&i2c_mock_device);
	if (ret < 0)
		return ret;

	mock_init(test, &i2c_mock->mock);
	adap->algo = &i2c_mock_algorithm;
	adap->dev.parent = &i2c_mock_device;
	snprintf(adap->name, sizeof(adap->name), "i2c_mock");

	ret = mock_set_default_action(mock, functionality,
				      int_return(test, I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL |
						 I2C_FUNC_SMBUS_READ_BLOCK_DATA));
	if (ret < 0)
		return ret;

	ret = i2c_add_adapter(adap);
	if (ret < 0)
		return ret;

	return 0;
}
