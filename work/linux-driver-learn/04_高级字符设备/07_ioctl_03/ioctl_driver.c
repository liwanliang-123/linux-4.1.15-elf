#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

#include <linux/miscdevice.h>
#include <linux/time.h>

#define TIME_OPEN_CMD _IO('a', 0)
#define TIME_SET_CMD _IOW('a', 1, int)
#define TIME_CLOSE_CMD _IO('a', 2)

static int val;
static void test_timer_handler(unsigned long dummy);
static DEFINE_TIMER(test_timer, test_timer_handler, 0, 0);

static void test_timer_handler(unsigned long dummy)
{
     printk("func: %s, line: %d\n", __func__, __LINE__);
     mod_timer(&test_timer, jiffies + msecs_to_jiffies(val));
}

static int test_ioctl_open(struct inode *inode, struct file *file)
{
     return 0;
}

static long test_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
     switch (cmd) {
     case TIME_OPEN_CMD:
          add_timer(&test_timer);
          break;
     case TIME_SET_CMD:
          // if(copy_from_user(&val, arg, sizeof(val)) != 0) {
          //      printk("copy_from_user error\n");
          //      return -1;
          // }
          val = arg;
          test_timer.expires = jiffies + msecs_to_jiffies(val);
          break;
     case TIME_CLOSE_CMD:
          del_timer(&test_timer);
          break;

     default:
          break;
     }

     return 0;
}

static int test_ioctl_release(struct inode *inode, struct file *file)
{
     del_timer(&test_timer);
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
     del_timer(&test_timer);
     misc_deregister(&test_ioctl_driver);
}

module_init(test_ioctl_init);
module_exit(test_ioctl_exit);

MODULE_LICENSE("GPL v2");
