#include <linux/init.h>
#include <linux/module.h>

#include <linux/atomic.h>
#include <asm/atomic.h>

#include <linux/fs.h>
#include <linux/miscdevice.h>

/*
     该驱动程序主要功能是防止驱动程序在同一时间段被多个用户程序使用
     只有当上一个程序调用 close 并调用驱动的 atomic_release 函数将原子变量再加一时
     另外一个驱动程序才能够成功调用该驱动的 open 函数
 */

struct atomic_test {
};

struct atomic_test at;
static atomic_t v = ATOMIC_INIT(1);  // 初始化 v = 1

static int atomic_open(struct inode *inode, struct file *file)
{
     printk("func:%s , line:%d\n", __func__, __LINE__);
//   atomic_dec_and_test 会将原子变量 -1，如果为 0，则这个函数为真
     if(!atomic_dec_and_test(&v)) {
          atomic_inc(&v);  // 原子的加1
          return -EBUSY;
     }

//   从这里开始下面才是多这个驱动真正的操作
     printk("func:%s , line:%d\n", __func__, __LINE__);

     file->private_data = &at;

     return 0;
}

static int atomic_release(struct inode *inode, struct file *file)
{
//   在这里将原子变量 V 的值再加回去,不然另外一个用户程序就不能调用 open 
     atomic_inc(&v);
     return 0;
}

struct file_operations atomic_fops = {
     .owner = THIS_MODULE,
     .open = atomic_open,
     .release = atomic_release,
};

struct miscdevice atomic_misc = {
     .minor = 100,
	.name = "atomic_misc",
	.fops = &atomic_fops,
};

static int __init atomic_init(void)
{
     int ret;
     printk("func:%s , line:%d\n", __func__, __LINE__);
	ret = misc_register(&atomic_misc);
	if (ret < 0)
		return ret;

     return 0;
}

static void __exit atomic_exit(void)
{
     printk("func:%s , line:%d\n", __func__, __LINE__);
     misc_deregister(&atomic_misc);
}

module_init(atomic_init);
module_exit(atomic_exit);
 
MODULE_LICENSE("GPL");
