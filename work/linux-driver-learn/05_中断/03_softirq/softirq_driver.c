#include <linux/module.h>
#include <linux/init.h>

#include <linux/interrupt.h>
#include <linux/gpio.h>

/*
     软中断是实现中断下半部的方法之一，但软中断的中断资源有限，中断号不多，如下：
     enum
     {
          HI_SOFTIRQ=0,
          TIMER_SOFTIRQ,
          NET_TX_SOFTIRQ,
          NET_RX_SOFTIRQ,
          BLOCK_SOFTIRQ,
          BLOCK_IOPOLL_SOFTIRQ,
          TASKLET_SOFTIRQ,
          SCHED_SOFTIRQ,
          HRTIMER_SOFTIRQ,
          RCU_SOFTIRQ,    // Preferable RCU should always be the last softirq

          TEST_SOFTIRQ

          NR_SOFTIRQS
     };

     可以看到 TASKLET_SOFTIRQ 也是软中断中的一种，如果想使用软中断直接使用 tasklat 就可以了
     实现一个自己软中断：
          1、需要先在上面枚举中添加一个自己的软中断
          2、添加软中断函数
          3、因为内核不允许user直接使用 open_softirq 相关的函数，所以如果需要这样使用还需要用 EXPORT_SYMBLE(open_softirq)
          4、修改完源码之后还需要重新编译kernel，这里就没有继续往下做实验了，因为有 tasklet,所以感觉这种方法没有必要
*/

struct softirq_driver {
     int gpio;
     int irq;
};

static struct softirq_driver sd;

static void softirq_func(struct softirq_action* data)
{
    printk("softirq_func\n");
}

static irqreturn_t test_handler(int irq, void *dev_id)
{
     printk("test_handler triggered, irq = %d\n", sd.irq);
     raise_softirq(TEST_SOFTIRQ); // 打开软中断
     return IRQ_RETVAL(IRQ_HANDLED);
}

static int __init softirq_driver_init(void)
{
     printk("softirq_driver init\n");

     sd.gpio = 137;
     sd.irq = gpio_to_irq(sd.gpio);
     printk("========> gpio = %d, irq = %d\n", sd.gpio, sd.irq);
                                            // 上升沿触发
     if (request_irq(sd.irq, test_handler, IRQF_TRIGGER_RISING, "interrupt_test", NULL) < 0) {
        printk("request_irq error\n");
        return -EINVAL;
     }

     //  初始化
     open_softirq(TEST_SOFTIRQ, softirq_func);

     return 0;
}

static void __exit softirq_driver_exit(void)
{
     printk("softirq_driver Exit\n");
     free_irq(sd.irq, NULL);
     raise_softirq_irqoff(TEST_SOFTIRQ);  // 关闭软中断
}

module_init(softirq_driver_init);
module_exit(softirq_driver_exit);

MODULE_LICENSE("GPL");
