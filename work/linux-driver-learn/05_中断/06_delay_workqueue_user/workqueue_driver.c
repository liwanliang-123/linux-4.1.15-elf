#include <linux/module.h>
#include <linux/init.h>

#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/workqueue.h>
#include <linux/delay.h>

struct delay_workqueue_driver {
     int gpio;
     int irq;
     struct delayed_work work_test;
     struct workqueue_struct *wq;
};

static struct delay_workqueue_driver wd;

static void workqueue_func(struct work_struct *work)
{
    printk("workqueue_func+\n");
    msleep(3000);
    printk("workqueue_func-\n");
}

static irqreturn_t test_handler(int irq, void *dev_id)
{
     printk("test_handler triggered, irq = %d\n", wd.irq);
     queue_delayed_work(wd.wq, &wd.work_test, /* delay time = */ 3 * HZ); // 延时 3 秒再去执行工作
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

     wd.wq = create_workqueue("test_workqueue");          // 创建自定义工作队列
     INIT_DELAYED_WORK(&wd.work_test, workqueue_func);    // 将延时工作添加到工作队列

     return 0;
}

static void __exit workqueue_driver_exit(void)
{
     printk("workqueue_driver Exit\n");
     free_irq(wd.irq, NULL);
     cancel_delayed_work_sync(&wd.work_test);  // 先取消调度工作任务
     flush_workqueue(wd.wq);                   // 刷新工作队列，告诉内核尽快处理工作队列上面的任务
     destroy_workqueue(wd.wq);                 // 删除自定义的工作队列
}

module_init(workqueue_driver_init);
module_exit(workqueue_driver_exit);

MODULE_LICENSE("GPL");
