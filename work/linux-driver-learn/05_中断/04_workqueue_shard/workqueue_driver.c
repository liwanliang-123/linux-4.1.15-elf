#include <linux/module.h>
#include <linux/init.h>

#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/workqueue.h>
#include <linux/delay.h>

/*
     工作队列：
          1、工作队列是实现中断下半部的方式之一，将工作推后执行
          2、和 tasklet 的主要区别就是 workqueue 能够休眠，但 tasklet 不能
          3、workqueue 会把工作交给内核内核线程去执行，linux 在启动的时候会创建一个内核线程，
               创建成功会处于 sleep 状态，当有工作需要执行时，再去唤醒这个内核线程
          4、工作队列分为共享工作队列和自定义工作队列（共享就是使用内核自动创建的线程去执行工作，而自定义就是用户自己创建这个线程去执行工作）
*/

struct workqueue_driver {
     int gpio;
     int irq;
     struct work_struct work_test;
};

static struct workqueue_driver wd;

static void workqueue_func(struct work_struct *work)
{
    printk("workqueue_func+\n");
    msleep(2000);
    printk("workqueue_func-\n");
}

static irqreturn_t test_handler(int irq, void *dev_id)
{
     printk("test_handler triggered, irq = %d\n", wd.irq);
     schedule_work(&wd.work_test);
     return IRQ_RETVAL(IRQ_HANDLED);
}

static int __init workqueue_driver_init(void)
{
     printk("workqueue_driver init\n");

     wd.gpio = 137;
     wd.irq = gpio_to_irq(wd.gpio);
     printk("========> gpio = %d, irq = %d\n", wd.gpio, wd.irq);
                                            // 上升沿触发
     if (request_irq(wd.irq, test_handler, IRQF_TRIGGER_RISING, "interrupt_test", NULL) < 0) {
        printk("request_irq error\n");
        return -EINVAL;
     }

     INIT_WORK(&wd.work_test, workqueue_func);

     return 0;
}

static void __exit workqueue_driver_exit(void)
{
     printk("workqueue_driver Exit\n");
     free_irq(wd.irq, NULL);
     cancel_work_sync(&wd.work_test);
}

module_init(workqueue_driver_init);
module_exit(workqueue_driver_exit);

MODULE_LICENSE("GPL");
