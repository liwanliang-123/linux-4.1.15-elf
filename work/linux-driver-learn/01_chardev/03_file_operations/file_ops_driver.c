#include <linux/init.h>
#include <linux/module.h>

#include <linux/fs.h>
#include <linux/kdev_t.h>
#include <linux/cdev.h>

/*
root@ELF1:~# mknod /dev/test c 247 0
root@ELF1:~# 
root@ELF1:~# ls /dev/test
/dev/test
root@ELF1:~# 
root@ELF1:~#  ./out 
func: file_ops_open
func: file_ops_release

*/

static dev_t dev_num;

struct cdev cdev_test;

static int file_ops_open(struct inode *inode, struct file *file)
{
     printk("func: %s\n", __func__);
     return 0;
}

static ssize_t file_ops_read(struct file *file, char __user *buf, size_t size, loff_t *off)
{
     printk("func: %s\n", __func__);
     return 0;
}

static ssize_t file_ops_write(struct file *file, const char __user *buf, size_t size, loff_t *off)
{
     printk("func: %s\n", __func__);
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
