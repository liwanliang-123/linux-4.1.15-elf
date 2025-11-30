#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

#include <linux/miscdevice.h>
#include <linux/timer.h>

#include <linux/poll.h>
#include <linux/signal.h>

struct fasync_struct *fapp;
static int sec[1];
static void test_timer_handler(unsigned long dummy);
static DEFINE_TIMER(test_timer, test_timer_handler, 0, 0);

static void test_timer_handler(unsigned long dummy)
{
     mod_timer(&test_timer, jiffies + msecs_to_jiffies(1000));
     sec[0] += 1;
//  通知用户空间内核有数据来了
     printk("kernel =============> %d\n", sec[0]);
     kill_fasync(&fapp, SIGIO, POLLIN);  // POLLIN 表示可写
}

static int second_open(struct inode *inode, struct file *file)
{
     test_timer.expires = jiffies + msecs_to_jiffies(0);
     add_timer(&test_timer);
     return 0;
}

static ssize_t second_read(struct file *file, char __user *buf, size_t size, loff_t *loff)
{
     if(copy_to_user(buf, &sec[0], sizeof(sec[0])) != 0) {
          printk("copy_to_user error\n");
          return -1;
     }
     return 0;
}

static ssize_t second_write(struct file *file, const char __user *buf, size_t size, loff_t *loff)
{
     return 0;
}

static int second_fasync(int fd, struct file *file, int on)
{
     return fasync_helper(fd, file, on, &fapp);
}

static int second_release(struct inode *inode, struct file *file)
{
     del_timer(&test_timer);
     return 0;
}

static struct file_operations second_fops = {
     .owner = THIS_MODULE,
     .open = second_open,
     .read = second_read,
     .write = second_write,
     .release = second_release,
     .fasync = second_fasync,
};

static struct miscdevice second_driver = {
     .name  = "second_driver",
     .fops  = &second_fops,
};

static int __init second_init(void)
{
     return misc_register(&second_driver);
}

static void __exit second_exit(void)
{
     misc_deregister(&second_driver);
}

module_init(second_init);
module_exit(second_exit);

MODULE_LICENSE("GPL v2");
