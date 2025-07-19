#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>

#include <linux/semaphore.h>
#include <linux/slab.h>

/*
信号量：semaphore
特点：临界资源区可以非常大，在open中上锁，在release中才释放
     而且不会一直等待，会引起休眠，所以不能在中断上下文中使用
注意：信号量的值不能小于等于0

(当信号量的值为1时，作用和特点和互斥锁一样)
*/

struct semaphore_test {
     struct semaphore semlock;
};

static struct semaphore_test *st;

static int semaphore_open(struct inode *inode, struct file *file)
{
     printk("func:%s , line:%d\n", __func__, __LINE__);

#if 0
//   信号量减一
     down(&st->semlock);  //不能被打断
#endif
//   减一
     if(down_interruptible(&st->semlock)) {  // 可以被打断（Ctrl + C）
          return -EINTR;
     }

     printk("func:%s , line:%d\n", __func__, __LINE__);
     file->private_data = &st;

     return 0;
}

static int semaphore_release(struct inode *inode, struct file *file)
{
     printk("func:%s , line:%d\n", __func__, __LINE__);

     up(&st->semlock);  // 访问完了还需要将信号量加一，好其它程序访问该驱动

     return 0;
}

struct file_operations semaphore_fops = {
     .owner = THIS_MODULE,
     .open = semaphore_open,
     .release = semaphore_release,
};

struct miscdevice semaphore_misc = {
     .minor = 100,
	.name = "semaphore_misc",
	.fops = &semaphore_fops,
};

static int __init semaphore_init(void)
{
     int ret;
     printk("func:%s , line:%d\n", __func__, __LINE__);

     st = kmalloc(sizeof(struct semaphore_test), GFP_ATOMIC);
//   初始化信号量
//   第二个参数表示一个房间有几把钥匙，这个钥匙可以有多个，只要能拿到该钥匙(down_interruptible)就能访问临界资源
//   当钥匙用完之后还需要把钥匙还回去，也就是up(&st->semlock)函数
//   这里初始化了一把钥匙,所以同一时间只能有一个应用程序访问该驱动
     sema_init(&st->semlock, 1);

	ret = misc_register(&semaphore_misc);
	if (ret < 0)
		return ret;

     return 0;
}

static void __exit semaphore_exit(void)
{
     printk("func:%s , line:%d\n", __func__, __LINE__);
     misc_deregister(&semaphore_misc);
}

module_init(semaphore_init);
module_exit(semaphore_exit);
 
MODULE_LICENSE("GPL");
