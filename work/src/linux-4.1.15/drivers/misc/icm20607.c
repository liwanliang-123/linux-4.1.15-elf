#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/spi/spi.h>
#include <linux/regmap.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <linux/icm20607.h>

#define DEVICE_NAME_SIZE 32
#define DEVICE_CNT 1

static unsigned char DEVICE_NAME[DEVICE_NAME_SIZE] = {0};

struct icm_dev{
	struct spi_device *spi_dev;	/* spi设备 */
	dev_t dev_id;	/* 设备号 */
	int major;	/* 主设备号 */
	int minor;	/* 次设备号 */
	struct cdev cdev;	/* cdev */
	struct class *class;	/* 类 */
	struct device *device;	/* 设备 */
	struct device_node *nd;	/* 设备节点 */
	signed int gyro_x_adc;	/* 陀螺仪X轴原始值 */
	signed int gyro_y_adc;	/* 陀螺仪Y轴原始值 */
	signed int gyro_z_adc;	/* 陀螺仪Z轴原始值 */
	signed int accel_x_adc;	/* 加速度计X轴原始值 */
	signed int accel_y_adc;	/* 加速度计Y轴原始值 */
	signed int accel_z_adc;	/* 加速度计Z轴原始值 */
	signed int temp_adc;	/* 温度值 */
	struct regmap *spi_regmap;	/* regmap */
	struct regmap_config spi_regmap_config;
	unsigned int Sensor_id;	/*Sensor id*/
};

static int icm_spi_read(struct icm_dev *dev, unsigned int reg, unsigned int *val)
{
	return regmap_read(dev->spi_regmap,reg,val);
}

static int icm_spi_write(struct icm_dev *dev, unsigned int reg, unsigned int val)
{
	return regmap_write(dev->spi_regmap,reg,val);
}

static void icm20607_readdata(struct icm_dev *dev)
{
	u8 ret;
	unsigned char data[14];

	ret = regmap_bulk_read(dev->spi_regmap,ICM20607_ACCEL_XOUT_H,data,14);

	dev->accel_x_adc = (signed short)((data[0] << 8) | data[1]);
	dev->accel_y_adc = (signed short)((data[2] << 8) | data[3]);
	dev->accel_z_adc = (signed short)((data[4] << 8) | data[5]);

	dev->temp_adc = (signed short)((data[6] << 8) | data[7]);

	dev->gyro_x_adc  = (signed short)((data[8] << 8) | data[9]);
	dev->gyro_y_adc  = (signed short)((data[10] << 8) | data[11]);
	dev->gyro_z_adc  = (signed short)((data[12] << 8) | data[13]);
}

static void icm42670_readdata(struct icm_dev *dev)
{
	u8 ret;
	unsigned char data[14];

	ret = regmap_bulk_read(dev->spi_regmap,ICM42670_TEMP_DATA_H,data,14);

	dev->temp_adc = (signed short)((data[0] << 8) | data[1]);

	dev->accel_x_adc = (signed short)((data[2] << 8) | data[3]);
	dev->accel_y_adc = (signed short)((data[4] << 8) | data[5]);
	dev->accel_z_adc = (signed short)((data[6] << 8) | data[7]);

	dev->gyro_x_adc = (signed short)((data[8] << 8) | data[9]);
	dev->gyro_y_adc  = (signed short)((data[10] << 8) | data[11]);
	dev->gyro_z_adc  = (signed short)((data[12] << 8) | data[13]);
}

static int device_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int device_release(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t device_read(struct file *file, char __user *buffer, size_t length, loff_t *offset)
{
	signed int data[7];
	long err = 0;
	struct cdev *cdev = file->f_path.dentry->d_inode->i_cdev;
	struct icm_dev *dev = container_of(cdev,struct icm_dev,cdev);

	if(dev->Sensor_id == ICM42670_ID)
		icm42670_readdata(dev);
	else if(dev->Sensor_id == ICM20607607_ID)
		icm20607_readdata(dev);
	else
		return -EIO;

	data[0] = dev->gyro_x_adc;
	data[1] = dev->gyro_y_adc;
	data[2] = dev->gyro_z_adc;
	data[3] = dev->accel_x_adc;
	data[4] = dev->accel_y_adc;
	data[5] = dev->accel_z_adc;
	data[6] = dev->temp_adc;
	err = copy_to_user(buffer,data,sizeof(data));

	return 0;
}

static int icm_reginit(struct icm_dev *dev)
{
	if(dev->Sensor_id == ICM42670_ID){
		icm_spi_write(dev,ICM42670_PWR_MGMT0,0x00);
		mdelay(50);
		icm_spi_write(dev,ICM42670_PWR_MGMT0,0x0f);
		mdelay(50);

		/* ±2000dps 50HZ */
		icm_spi_write(dev,ICM42670_GYRO_CONFIG0,0x0A);
		/* ±16G 50HZ*/
		icm_spi_write(dev,ICM42670_ACCEL_CONFIG0,0x0A);
	}else if(dev->Sensor_id == ICM20607607_ID){
		icm_spi_write(dev,ICM20607_PWR_MGMT_1,0x80);
		mdelay(50);
		icm_spi_write(dev,ICM20607_PWR_MGMT_1,0x01);
		mdelay(50);

		/* ±2000dps */
		icm_spi_write(dev,ICM20607_GYRO_CONFIG,0x18);
		/* ±16G */
		icm_spi_write(dev,ICM20607_ACCEL_CONFIG,0x18);
		/* 陀螺仪低通滤波BW=20Hz */
		icm_spi_write(dev,ICM20607_CONFIG,0x04);
		/* 加速度计低通滤波BW=21.2Hz */
		icm_spi_write(dev,ICM20607_ACCEL_CONFIG2,0x04);
		/* 打开加速度计和陀螺仪所有轴 */
		icm_spi_write(dev,ICM20607_PWR_MGMT_2,0x00);
		/* 关闭低功耗 */
		icm_spi_write(dev,ICM20607_LP_MODE_CFG,0x00);
		/* 关闭FIFO */
		icm_spi_write(dev,ICM20607_FIFO_EN,0x00);
	}else
		return -EIO;
	return 0;
}

static struct file_operations fops = {
	.owner = THIS_MODULE,
	.open = device_open,
	.release = device_release,
	.read = device_read,
};

static int icm_probe(struct spi_device *spi)
{
	struct icm_dev *icmdev;
	int ret;

	icmdev = devm_kzalloc(&spi->dev,sizeof(*icmdev),GFP_KERNEL);
	if(!icmdev)
		return -ENOMEM;

	icmdev->spi_regmap_config.reg_bits = 8;
	icmdev->spi_regmap_config.val_bits = 8;
	icmdev->spi_regmap_config.read_flag_mask = 0x80;

	/* regmap_init */
	icmdev->spi_regmap = devm_regmap_init_spi(spi,&icmdev->spi_regmap_config);
	if (IS_ERR(icmdev->spi_regmap))
		return PTR_ERR(icmdev->spi_regmap);

	/* read Sensor id */
	icm_spi_read(icmdev, ICM_REG_WHO_AM_I ,&icmdev->Sensor_id);
	if(icmdev->Sensor_id == ICM42670_ID)
		sprintf(DEVICE_NAME,"%s","icm42670");
	else
		sprintf(DEVICE_NAME,"%s","icm20607");

	/* alloc chrdev */
	ret = alloc_chrdev_region(&icmdev->dev_id,0,DEVICE_CNT,DEVICE_NAME);
	if (ret < 0) {
		pr_err(KERN_ALERT "Failed to allocate device number: %d\n",ret);
		return ret;
	}

	/* cdev */
	icmdev->cdev.owner = THIS_MODULE;
	cdev_init(&icmdev->cdev,&fops);
	ret = cdev_add(&icmdev->cdev,icmdev->dev_id,1);
	if (ret < 0)
		goto DEL_CHRDEV;

	/* class */
	icmdev->class = class_create(THIS_MODULE,DEVICE_NAME);
	if (IS_ERR(icmdev->class))
		goto DEL_CDEV;

	/* create a device */
	icmdev->device = device_create(icmdev->class,NULL,icmdev->dev_id,NULL,
						DEVICE_NAME);
	if (IS_ERR(icmdev->device))
		goto DEL_DEVICE;

	/* init spi_device */
	icmdev->spi_dev = spi;
	spi->mode = SPI_MODE_0;
	spi_setup(spi);

	/* reg init */
	ret = icm_reginit(icmdev);
	if(ret)
		return ret;

	spi_set_drvdata(spi, icmdev);

	return 0;

DEL_DEVICE:
	device_destroy(icmdev->class, icmdev->dev_id);
DEL_CDEV:
	cdev_del(&icmdev->cdev);
DEL_CHRDEV:
	unregister_chrdev_region(icmdev->dev_id, 1);;
	return -EIO;
}

static int icm_remove(struct spi_device *spi)
{
	struct icm_dev *icmdev = spi_get_drvdata(spi);

	/* del cdev */
	cdev_del(&icmdev->cdev);

	/* del chrdev */
	unregister_chrdev_region(icmdev->dev_id, 1);

	/* del device */
	device_destroy(icmdev->class, icmdev->dev_id);

	/* del class */
	class_destroy(icmdev->class);

	return 0;
}
static const struct of_device_id of_platform_match[] = {
		{ .compatible = "elf,icm", },
		{},
};

static struct spi_driver icm_driver = {
	.driver = {
		.name = "icm",
		.owner = THIS_MODULE,
	.of_match_table = of_platform_match,
	},
	.probe = icm_probe,
	.remove = icm_remove,
};

static int __init icm_init(void)
{
	int ret;

	ret = spi_register_driver(&icm_driver);
	if (ret < 0) {
		pr_err("Failed to register ICM42670 driver: %d\n", ret);
		return ret;
	}

	return 0;
}

static void __exit icm_exit(void)
{
	spi_unregister_driver(&icm_driver);
}

module_init(icm_init);
module_exit(icm_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("elf1");
MODULE_DESCRIPTION("ICM SPI device driver");
