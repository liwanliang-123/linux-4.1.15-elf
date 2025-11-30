#include <linux/module.h>
#include <linux/init.h>

#include <linux/time.h>


static void test_timer_handler(unsigned long dummy)
{
     printk("func: %s, line: %d\n", __func__, __LINE__);
//  内核的定时器不是周期性的，如果想再次定时，需要在超时函数中再次开启

}

static DEFINE_TIMER(test_timer, test_timer_handler, 0, 0);

static int __init timer_init(void)
{
     printk("timer_init init\n");
                        //当前时间 + 要定时的时间 （5秒）
     test_timer.expires = jiffies + msecs_to_jiffies(5000);

     add_timer(&test_timer);

     return 0;
}

static void __exit timer_exit(void)
{
     printk("timer_exit Exit\n");

     del_timer(&test_timer);

}

module_init(timer_init);
module_exit(timer_exit);

MODULE_LICENSE("GPL");
