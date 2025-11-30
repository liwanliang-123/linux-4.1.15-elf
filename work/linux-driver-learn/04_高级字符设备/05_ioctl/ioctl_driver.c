#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

#include <linux/miscdevice.h>

#define CMD_TEST0 _IO('a', 0)
#define CMD_TEST1 _IOW('a', 1, int)
#define CMD_TEST2 _IOR('a', 2, int)

static int test_ioctl_open(struct inode *inode, struct file *file)
{
     return 0;
}

static long test_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
     int val;

     switch (cmd) {
     case CMD_TEST0:
          printk("======> line: %d\n", __LINE__);
          break;
     case CMD_TEST1:
          printk("======> line: %d arg = %lu\n", __LINE__, arg);
          break;
     case CMD_TEST2:
          val = 100;
          if(copy_to_user(arg, &val, sizeof(val)) != 0) {
               printk("copy_to_user error\n");
               return -1;
          }
          break;

     default:
          break;
     }

     return 0;
}

static int test_ioctl_release(struct inode *inode, struct file *file)
{
     return 0;
}

static struct file_operations test_ioctl_fops = {
     .owner = THIS_MODULE,
     .open = test_ioctl_open,
     .unlocked_ioctl = test_unlocked_ioctl,
     .release = test_ioctl_release,
};

static struct miscdevice test_ioctl_driver = {
     .name  = "ioctl_driver",
     .fops  = &test_ioctl_fops,
};

static int __init test_ioctl_init(void)
{
     return misc_register(&test_ioctl_driver);
}

static void __exit test_ioctl_exit(void)
{
     misc_deregister(&test_ioctl_driver);
}

module_init(test_ioctl_init);
module_exit(test_ioctl_exit);

MODULE_LICENSE("GPL v2");
