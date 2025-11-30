#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

#include <linux/miscdevice.h>

#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/poll.h>

static int flag;
DECLARE_WAIT_QUEUE_HEAD(read_wq);

static int misc_ops_open(struct inode *inode, struct file *file)
{
     printk("func: %s\n", __func__);
     return 0;
}

static ssize_t misc_ops_read(struct file *file, char __user *buf, size_t size, loff_t *off)
{
     char buf_test[32] = "hello world!\n";
     printk("func: %s\n", __func__);

     wait_event_interruptible(read_wq, flag);

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

     flag = 1;
     wake_up_interruptible(&read_wq);

     // printk("misc_ops_write : %s\n", buf_test);

     return 0;
}

static unsigned int misc_ops_poll(struct file *file, struct poll_table_struct *p)
{
     int mask = 0;

     poll_wait(file, &read_wq, p);  // 这个函数不会阻塞
     if(flag == 1) {
          mask |= POLLIN;
     }

     return mask;
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
     .poll = misc_ops_poll,
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

     flag = 0;

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
