#include <linux/module.h>
#include <linux/init.h>

#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/workqueue.h>
#include <linux/delay.h>

// 打包数据包
struct work_data {
     int a;
     int b;
     struct work_struct work_test;
};

struct workqueue_driver {
     int gpio;
     int irq;
     struct work_data w_data;
     struct workqueue_struct *wq;
};

static struct workqueue_driver wd;

static void workqueue_func(struct work_struct *work)
{
     // 取数据包
     struct work_data *pdata = container_of(work, struct work_data, work_test);

     printk("======> a = %d\n", pdata->a);
     printk("======> b = %d\n", pdata->b);
     printk("workqueue_func+\n");
     msleep(3000);
     printk("workqueue_func-\n");
}

static irqreturn_t test_handler(int irq, void *dev_id)
{
     printk("test_handler triggered, irq = %d\n", wd.irq);
     queue_work(wd.wq, &wd.w_data.work_test);  // 调度工作队列，即将工作 work_test 放到创建的 wq 上面
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

     wd.wq = create_workqueue("test_workqueue");  // 创建自定义工作队列
     INIT_WORK(&wd.w_data.work_test, workqueue_func);    // 添加工作
     wd.w_data.a = 100;
     wd.w_data.b = 1024;

     return 0;
}

static void __exit workqueue_driver_exit(void)
{
     printk("workqueue_driver Exit\n");
     free_irq(wd.irq, NULL);
     cancel_work_sync(&wd.w_data.work_test);  // 先取消调度工作任务
     flush_workqueue(wd.wq);                  // 刷新工作队列，告诉内核尽快处理工作队列上面的任务
     destroy_workqueue(wd.wq);                // 删除自定义的工作队列
}

module_init(workqueue_driver_init);
module_exit(workqueue_driver_exit);

MODULE_LICENSE("GPL");
