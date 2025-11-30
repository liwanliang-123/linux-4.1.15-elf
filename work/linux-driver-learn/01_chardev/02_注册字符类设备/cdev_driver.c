#include <linux/init.h>
#include <linux/module.h>

#include <linux/fs.h>
#include <linux/kdev_t.h>
#include <linux/cdev.h>

static dev_t dev_num;

struct cdev cdev_test;
struct file_operations cdev_ops = {
     .owner = THIS_MODULE,
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
     cdev_add(&cdev_test, dev_num, 1);

     return 0;
}

static void __exit char_exit(void)
{
     cdev_del(&cdev_test);
     unregister_chrdev_region(dev_num, 1);
     printk("func: %s\n", __func__);
}

module_init(char_init);
module_exit(char_exit);

MODULE_LICENSE("GPL v2");
