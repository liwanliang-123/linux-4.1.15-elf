#include <linux/init.h>
#include <linux/module.h>

#include <linux/fs.h>
#include <linux/kdev_t.h>

static int major = 0;
static int minor = 0;
static dev_t dev_num;

module_param(major, int, S_IRUGO);
module_param(minor, int, S_IRUGO);

static int __init char_init(void)
{
     int ret;

     if(major && minor) {
          printk("major = %d, minor = %d\n", major, minor);
          dev_num = MKDEV(major, minor);
          ret = register_chrdev_region(dev_num, 1, "static_my_char");
          if (ret < 0) {
               printk("register_chrdev_region is error!\n");
          }
     } else {
          ret = alloc_chrdev_region(&dev_num, 0, 1, "alloc_my_char");
          if (ret < 0) {
               printk("alloc_chrdev_region is error!\n");
          }
          major = MAJOR(dev_num);
          minor = MINOR(dev_num);
          printk("major = %d, minor = %d\n", major, minor);
     }

     return 0;
}

static void __exit char_exit(void)
{
     unregister_chrdev_region(dev_num, 1);
     printk("func: %s\n", __func__);
}

module_init(char_init);
module_exit(char_exit);

MODULE_LICENSE("GPL v2");
