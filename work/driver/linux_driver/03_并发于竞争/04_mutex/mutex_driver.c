#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>

#include <linux/mutex.h>
#include <linux/slab.h>

/*
注意：互斥锁会导致休眠，所以不能在中断上下文使用
     同一时刻只能有一个线程持有锁，并且只有持有者来解锁
     互斥锁相当于就相当于是信号量等于1的信号量
*/

struct mutex_test {
     struct mutex mutex;
};

static int flag = 1;
static struct mutex_test *mt;

static int mutex_open_test(struct inode *inode, struct file *file)
{
     printk("func:%s , line:%d\n", __func__, __LINE__);
     file->private_data = &mt;

     mutex_lock(&mt->mutex);
     printk("func:%s , line:%d\n", __func__, __LINE__);
     if(flag != 1) {
          mutex_unlock(&mt->mutex);
          return -EBUSY;
     }
     flag = 0;
     mutex_unlock(&mt->mutex);

     return 0;
}

static int mutex_release_test(struct inode *inode, struct file *file)
{
     printk("func:%s , line:%d\n", __func__, __LINE__);

     mutex_lock(&mt->mutex);
     flag = 1;
     mutex_unlock(&mt->mutex);

     return 0;
}

struct file_operations mutex_fops = {
     .owner = THIS_MODULE,
     .open = mutex_open_test,
     .release = mutex_release_test,
};

struct miscdevice mutex_misc = {
     .minor = 100,
	.name = "mutex_misc",
	.fops = &mutex_fops,
};

static int __init mutex_init_test(void)
{
     int ret;
     printk("func:%s , line:%d\n", __func__, __LINE__);

     mt = kmalloc(sizeof(struct mutex_test), GFP_ATOMIC);
     mutex_init(&mt->mutex);

	ret = misc_register(&mutex_misc);
	if (ret < 0)
		return ret;

     return 0;
}

static void __exit mutex_exit_test(void)
{
     printk("func:%s , line:%d\n", __func__, __LINE__);
     misc_deregister(&mutex_misc);
}

module_init(mutex_init_test);
module_exit(mutex_exit_test);

MODULE_LICENSE("GPL");
