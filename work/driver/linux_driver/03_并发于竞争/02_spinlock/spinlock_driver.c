#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>

#include <linux/spinlock.h>
#include <linux/slab.h>

struct spinlock_test {
     spinlock_t spin_l;
};

static int flag = 1;
static struct spinlock_test *at;

static int spinlock_open(struct inode *inode, struct file *file)
{
     printk("func:%s , line:%d\n", __func__, __LINE__);
     file->private_data = &at;

     spin_lock(&at->spin_l);
     printk("func:%s , line:%d\n", __func__, __LINE__);
//   这里就算获取到 spinlok 锁，但也不能操作 flag 
//   因为需要上一个应用程序调用 close 之后更新 flag 的值才行
     if(flag != 1) {
          spin_unlock(&at->spin_l);
          return -EBUSY;
     }
     flag = 0;

//   这里应该需要解锁，因为资源已经访问完了
//   如果不解锁，并且在用户空间不调用 close 函数时，就会发生死锁，因为一直没有释放锁
     spin_unlock(&at->spin_l);
     return 0;
}

static int spinlock_release(struct inode *inode, struct file *file)
{
     printk("func:%s , line:%d\n", __func__, __LINE__);

     spin_lock(&at->spin_l);
     flag = 1;
     spin_unlock(&at->spin_l);

     return 0;
}

struct file_operations spinlock_fops = {
     .owner = THIS_MODULE,
     .open = spinlock_open,
     .release = spinlock_release,
};

struct miscdevice spinlock_misc = {
     .minor = 100,
	.name = "spinlock_misc",
	.fops = &spinlock_fops,
};

static int __init spinlock_init(void)
{
     int ret;
     printk("func:%s , line:%d\n", __func__, __LINE__);

     at = kmalloc(sizeof(struct spinlock_test), GFP_ATOMIC);
     spin_lock_init(&at->spin_l);

	ret = misc_register(&spinlock_misc);
	if (ret < 0)
		return ret;

     return 0;
}

static void __exit spinlock_exit(void)
{
     printk("func:%s , line:%d\n", __func__, __LINE__);
     misc_deregister(&spinlock_misc);
}

module_init(spinlock_init);
module_exit(spinlock_exit);
 
MODULE_LICENSE("GPL");
