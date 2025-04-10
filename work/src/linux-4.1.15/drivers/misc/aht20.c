/* Copyright (C) 2020-2020
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
/*
 *  This is Linux kernel modules for ambient light sensor
 *  Revision History
 *  2020-11-07:    Ver. 1.0    New release 
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/i2c.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <asm/mach/map.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <asm/io.h>
#include <linux/device.h>

#include <linux/platform_device.h>

#include <linux/i2c_aht20.h>

#define DEV_NAME "aht20"
#define DEV_CNT (1)

static dev_t aht20_dev;
static struct cdev aht20_chr_dev;
struct class *class_aht20;
struct device *device_aht20;
struct device_node *aht20_device_node;

struct i2c_client *aht20_client = NULL;

/* Private typedef -----------------------------------------------------------*/
/* aht20设备结构体 */
typedef struct {
	dev_t devid;			/*!< 设备号 */
	struct cdev cdev;		/*!< cdev */
	struct class *class;	/*!< 类 */
	struct device *device;	/*!< 设备 */
	struct device_node *nd; /*!< 设备节点 */
	int major;				/*!< 主设备号 */
	void *private_data;		/*!< 私有数据 */
	unsigned short ir, als, ps;	/*!< 光传感数据 */
}aht20_dev_t;

/* Private variables ---------------------------------------------------------*/
static aht20_dev_t aht20dev;
uint8_t clibrate_arg[] = {AHT20_CMD_CALIBRATION_ARG0, AHT20_CMD_CALIBRATION_ARG1};
uint8_t trigger_arg[] = {AHT20_CMD_TRIGGER_ARG0,AHT20_CMD_TRIGGER_ARG1};

static s32 aht20_write_regs(aht20_dev_t *dev, u8 reg, u8 *buf, u8 len)
{
	u8 byte[256] = {0};
	struct i2c_msg msg;
	struct i2c_client *client = (struct i2c_client*)dev->private_data;

	byte[0] = reg;	/*!< 寄存器首地址 */
	memcpy(&byte[1], buf, len);	/*!< 拷贝数据 */
	
	msg.addr = client->addr;	/*!< aht20地址 */
	msg.flags = 0;	/*!< 标记为写数据 */
	msg.buf = byte;
	msg.len = len + 1;		/*!< 要写入数据的长度 */	
	
	return i2c_transfer(client->adapter, &msg, 1);
}
static int aht20_read_regs(aht20_dev_t *dev, u8 reg, void *val, int len)
{
	int ret = 0;

	struct i2c_msg msg[2];
	struct i2c_client *client = (struct i2c_client*)dev->private_data;

	/* msg[0]为发送要读取的首地址 */
	msg[0].addr = client->addr;	/*!< aht20地址 */
	msg[0].flags = 0;	/*!< 标记为发送数据 */
	msg[0].buf = &reg;	/*!< 读取的首地址 */
	msg[0].len = 1;		/*!< reg长度 */

	/* msg[1]为要读取数据 */
	msg[1].addr = client->addr;	/*!< aht20地址 */
	msg[1].flags = I2C_M_RD;	/*!< 标记为读取数据 */
	msg[1].buf = val;	/*!< 读取数据的缓冲区 */
	msg[1].len = len;		/*!< 读取数据的长度 */	

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret == 2)	ret = 0;
	else ret = -EREMOTEIO;

	return ret;
}
static void aht20_write_reg(aht20_dev_t *dev, u8 reg, u8 data)
{
	u8 buf = 0;
	buf = data;
	aht20_write_regs(dev, reg, &buf, 1);
}
static unsigned char aht20_read_reg(aht20_dev_t *dev, u8 reg)
{
	u8 data = 0;
	aht20_read_regs(dev, reg, &data, 1);
	return data;

#if 0
	struct i2c_client *client = (struct i2c_client *)dev->private_data;
	return i2c_smbus_read_byte_data(client, reg);
#endif
}
static int aht20_read_data(aht20_dev_t *dev, void *val, int len)
{
	int ret = 0;

	struct i2c_msg msg[2];
	struct i2c_client *client = (struct i2c_client*)dev->private_data;

	/* msg[1]为要读取数据 */
	msg[0].addr = client->addr;	/*!< aht20地址 */
	msg[0].flags = I2C_M_RD;	/*!< 标记为读取数据 */
	msg[0].buf = val;	/*!< 读取数据的缓冲区 */
	msg[0].len = len;		/*!< 读取数据的长度 */	

	return i2c_transfer(client->adapter, msg, 1);
}
static int aht20_get_measure(uint32_t* RAW)
{
	int retval = 0, i = 0;
	uint8_t data[7] = {0};

	retval = aht20_write_regs(&aht20dev,AHT20_CMD_TRIGGER,trigger_arg,2);
	msleep(AHT20_MEASURE_TIME);
	aht20_read_data(&aht20dev,data,7);
	
	for (i = 0; AHT20_STATUS_BUSY(data[0]) && i < AHT20_MAX_RETRY; i++) {
		printk("AHT20 device busy, retry %d/%d!\r\n", i, AHT20_MAX_RETRY);
		msleep(AHT20_MEASURE_TIME);
		aht20_read_data(&aht20dev,data,7);
    }
	if (i >= AHT20_MAX_RETRY) {
        printk("AHT20 device always busy!\r\n");
    }

	uint32_t humiRaw = data[1];
    humiRaw = (humiRaw << 8) | data[2];
    humiRaw = (humiRaw << 4) | ((data[3] & 0xF0) >> 4);

    uint32_t tempRaw = data[3] & 0x0F;
    tempRaw = (tempRaw << 8) | data[4];
    tempRaw = (tempRaw << 8) | data[5];
	RAW[0] = humiRaw;
	RAW[1] = tempRaw;
//	printk("aht20 humiRAW = %05X,  tempRAW = %05X\r\n", humiRaw, tempRaw);
	return 0;
}
/* send reset cmd */
static int aht20_write_reset(aht20_dev_t *dev)
{
	u8 byte[256] = {0};
	struct i2c_msg msg;
	struct i2c_client *client = (struct i2c_client*)dev->private_data;

	byte[0] = AHT20_CMD_RESET;	/*!< 寄存器首地址 */
	msg.addr = client->addr;	/*!< aht20地址 */
	msg.flags = 0;	/*!< 标记为写数据 */
	msg.buf = byte;	/*!< 要写入的数据缓冲区 */
	msg.len = 1;		/*!< 要写入数据的长度 */	
	
	return i2c_transfer(client->adapter, &msg, 1);
}

static int aht20_init(void)
{
	unsigned char status;
	int retval;
	float temp, humi;
	uint8_t RAW[7];
	status = aht20_read_reg(&aht20dev, AHT20_CMD_STATUS);

	if (AHT20_STATUS_BUSY(status) || !AHT20_STATUS_CALI(status)) {
		retval = aht20_write_reset(&aht20dev);
		msleep(AHT20_STARTUP_TIME);
		retval = aht20_write_regs(&aht20dev,AHT20_CMD_CALIBRATION,clibrate_arg,2);
		msleep(AHT20_CALIBRATION_TIME);
		return retval;
    }
	aht20_get_measure(&RAW[0]);
}

static int aht20_open(struct inode *inode, struct file *filp)
{
	filp->private_data = &aht20dev;
    return 0;
}
static ssize_t aht20_read(struct file *filp, char __user *buf, size_t cnt, loff_t *off)
{
	float temp,humi;
//	uint8_t data[7] = {0};
	uint32_t data[2] = {0};
	long err = 0;

	filp->private_data = &aht20dev;
	aht20_get_measure(&data[0]);
	err = copy_to_user(buf, data, sizeof(data));
    return 0;
}
static int aht20_release(struct inode *inode, struct file *filp)
{
    return 0;
}
static struct file_operations aht20_chr_dev_fops =
{
    .owner = THIS_MODULE,
    .open = aht20_open,
    .read = aht20_read,
    .release = aht20_release,
};

static int aht20_probe(struct i2c_client *client, const struct i2c_device_id *id)
{

	int ret = -1;
	ret = alloc_chrdev_region(&aht20dev.devid, 0, DEV_CNT, DEV_NAME);
	if (ret < 0)
	{
		printk("fail to alloc aht_dev\n");
		goto alloc_err;
	}

//	aht20_chr_dev.owner = THIS_MODULE;
	cdev_init(&aht20dev.cdev, &aht20_chr_dev_fops);

	ret = cdev_add(&aht20dev.cdev, aht20dev.devid, DEV_CNT);
	if (ret < 0)
	{
		printk("fail to add cdev\n");
		goto add_err;
	}

	aht20dev.class = class_create(THIS_MODULE, DEV_NAME);

	aht20dev.device = device_create(aht20dev.class, NULL, aht20dev.devid, NULL, DEV_NAME);
	aht20dev.private_data = client;
	aht20_init();
	return 0;

add_err:
	unregister_chrdev_region(aht20dev.devid, DEV_CNT);
	printk("\n add_err error! \n");
alloc_err:

	return -1;
}

static int aht20_remove(struct i2c_client *client)
{
	device_destroy(aht20dev.class, aht20dev.devid);	  
	class_destroy(aht20dev.class);	
	cdev_del(&aht20dev.cdev);
	unregister_chrdev_region(aht20dev.devid, DEV_CNT);
	return 0;
}

static const struct i2c_device_id aht20_device_id[] = {
    {"elf", 0},
    { }
};

/*定义设备树匹配表*/
static const struct of_device_id aht20_match_table[] = {
    {.compatible = "elf,aht20", },
    { },
};

/*定义i2c设备结构体*/
struct i2c_driver aht20_driver = {
    .probe = aht20_probe,
    .remove = aht20_remove,
    .id_table = aht20_device_id,
    .driver = {
            .name = "elf,aht20",
            .owner = THIS_MODULE,
            .of_match_table = aht20_match_table,
    },
};


static int __init aht20_driver_init(void)
{
    pr_info("aht20 driver init\n");
    return i2c_add_driver(&aht20_driver);
}
static void __exit aht20_driver_exit(void)
{
    pr_info("aht20 driver exit\n");
    i2c_del_driver(&aht20_driver);
}
module_init(aht20_driver_init);
module_exit(aht20_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("bkxr@outlook.com");
MODULE_DESCRIPTION("aht20 sensor driver");


