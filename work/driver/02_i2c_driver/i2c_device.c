#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/slab.h>

static struct i2c_client *i2c_client;

// 这里使用 i2c_board_info 来描述设备
// 而在设备树中则使用的是设备节点来描述一个设备
// 其本质都是一样的
static struct i2c_board_info my_i2c_dev_info[] __initdata = {
    {
        I2C_BOARD_INFO("myi2c", 0x38)
    },
};

static int i2c_dev_init(void)
{
    struct i2c_adapter *i2c_adap;

    // 找到一个 i2c 适配器，1表示用的 i2c1 控制器
    i2c_adap = i2c_get_adapter(1);

    // 在找到的适配器下面新建一个i2c设备
    // 并通过 i2c_board_info 来描述设备名和地址
    i2c_client = i2c_new_device(i2c_adap, my_i2c_dev_info);
    i2c_put_adapter(i2c_adap);

    return 0;
}

static void i2c_dev_exit(void)
{
    i2c_unregister_device(i2c_client);
}

module_init(i2c_dev_init);
module_exit(i2c_dev_exit);
MODULE_LICENSE("GPL");