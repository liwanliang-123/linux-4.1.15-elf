#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

#include <linux/miscdevice.h>

#include <linux/poll.h>
#include <linux/signal.h>

struct fasync_struct *fapp;

static int misc_ops_open(struct inode *inode, struct file *file)
{
     printk("func: %s\n", __func__);
     return 0;
}

static ssize_t misc_ops_read(struct file *file, char __user *buf, size_t size, loff_t *off)
{
     char buf_test[32] = "888888888888888888888888\n";
     printk("func: %s\n", __func__);

//  内核空间再把数据读取到用户空间
     if(copy_to_user(buf, &buf_test, sizeof(buf_test)) != 0) {
          printk("copy_to_user error\n");
          return -1;
     }

     return 0;
}

static ssize_t misc_ops_write(struct file *file, const char __user *buf, size_t size, loff_t *off)
{
     char buf_test[32];
     printk("func: %s\n", __func__);

     if(copy_from_user(buf_test, buf, size) != 0) {
          printk("copy_from_user error\n");
          return -1;
     }

//  通知用户空间内核有数据来了
     kill_fasync(&fapp, SIGIO, POLLIN);  // POLLIN 表示可写

     return 0;
}

static int misc_ops_fasync(int fd, struct file *file, int on)
{
     return fasync_helper(fd, file, on, &fapp);
}

static int misc_ops_release(struct inode *inode, struct file *file)
{
     printk("func: %s\n", __func__);
     return 0;
}

struct file_operations misc_ops = {
     .owner = THIS_MODULE,
     .open = misc_ops_open,
     .read = misc_ops_read,
     .write = misc_ops_write,
     .release = misc_ops_release,
     .fasync = misc_ops_fasync,
};

struct miscdevice misc_dev = {
     .name = "test",
     .minor = MISC_DYNAMIC_MINOR,
     .fops = &misc_ops,
};

static int __init misc_init(void)
{
     int ret;

     ret = misc_register(&misc_dev);
     if(ret < 0) {
          printk("misc register error!\n");
          return -1;
     }

     return 0;
}

static void __exit misc_exit(void)
{
     misc_deregister(&misc_dev);
     printk("func: %s\n", __func__);
}

module_init(misc_init);
module_exit(misc_exit);

MODULE_LICENSE("GPL v2");
