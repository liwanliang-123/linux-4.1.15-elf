#include <linux/module.h>
#include <linux/init.h>

#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/workqueue.h>
#include <linux/delay.h>

struct workqueue_driver {
     int gpio;
     int irq;
     struct work_struct work_test;
     struct workqueue_struct *wq;
};

static struct workqueue_driver wd;

static void workqueue_func(struct work_struct *work)
{
    printk("workqueue_func+\n");
    msleep(3000);
    printk("workqueue_func-\n");
}

static irqreturn_t test_handler(int irq, void *dev_id)
{
     printk("test_handler triggered, irq = %d\n", wd.irq);
     queue_work(wd.wq, &wd.work_test);  // 调度工作队列，即将工作 work_test 放到创建的 wq 上面
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

     // 主要添加 alloc_workqueue 函数, WQ_UNBOUND 表示该工作队列不和 CPU 绑定，任何 CPU 都可以执行
     wd.wq = alloc_workqueue("test_workqueue", WQ_UNBOUND, 0);  // 创建自定义工作队列
     INIT_WORK(&wd.work_test, workqueue_func);    // 添加工作

     return 0;
}

static void __exit workqueue_driver_exit(void)
{
     printk("workqueue_driver Exit\n");
     free_irq(wd.irq, NULL);
     cancel_work_sync(&wd.work_test);  // 先取消调度工作任务
     flush_workqueue(wd.wq);           // 刷新工作队列，告诉内核尽快处理工作队列上面的任务
     destroy_workqueue(wd.wq);         // 删除自定义的工作队列
}

module_init(workqueue_driver_init);
module_exit(workqueue_driver_exit);

MODULE_LICENSE("GPL");
