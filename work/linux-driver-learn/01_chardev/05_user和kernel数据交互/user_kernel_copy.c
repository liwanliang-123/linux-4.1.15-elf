#include <linux/init.h>
#include <linux/module.h>

#include <linux/fs.h>
#include <linux/kdev_t.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>

static dev_t dev_num;

struct cdev cdev_test;
struct class *class;
struct device *device;

static int file_ops_open(struct inode *inode, struct file *file)
{
     printk("func: %s\n", __func__);
     return 0;
}

static ssize_t file_ops_read(struct file *file, char __user *buf, size_t size, loff_t *off)
{
     char buf_test[32] = "hello world!\n";
     printk("func: %s\n", __func__);

     if(copy_to_user(buf, &buf_test, sizeof(buf_test)) != 0) {
          printk("copy_to_user error\n");
          return -1;
     }

     return 0;
}

static ssize_t file_ops_write(struct file *file, const char __user *buf, size_t size, loff_t *off)
{
     char buf_test[32];
     printk("func: %s\n", __func__);

     if(copy_from_user(buf_test, buf, size) != 0) {
          printk("copy_from_user error\n");
          return -1;
     }

     printk("file_ops_write : %s\n", buf_test);

     return 0;
}

static int file_ops_release(struct inode *inode, struct file *file)
{
     printk("func: %s\n", __func__);
     return 0;
}

struct file_operations cdev_ops = {
     .owner = THIS_MODULE,
     .open = file_ops_open,
     .read = file_ops_read,
     .write = file_ops_write,
     .release = file_ops_release,
};

static int __init char_init(void)
{
     int ret;

     ret = alloc_chrdev_region(&dev_num, 0, 1, "alloc_my_char");
     if (ret < 0) {
          printk("alloc_chrdev_region is error!\n");
     }

     cdev_test.owner = THIS_MODULE;
     cdev_init(&cdev_test, &cdev_ops);
     cdev_add(&cdev_test, dev_num, 1);  // 向系统中添加一个字符设备

     // 为了让它自动的创建设备节点，就需要下面的操作
     class = class_create(THIS_MODULE, "class_test");
     device = device_create(class, NULL, dev_num, NULL, "test");

     return 0;
}

static void __exit char_exit(void)
{
     device_destroy(class, dev_num);
     class_destroy(class);

     cdev_del(&cdev_test);
     unregister_chrdev_region(dev_num, 1);

     printk("func: %s\n", __func__);
}

module_init(char_init);
module_exit(char_exit);

MODULE_LICENSE("GPL v2");
