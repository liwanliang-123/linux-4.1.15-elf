#include <linux/module.h>
#include <linux/init.h>

#include <linux/interrupt.h>
#include <linux/gpio.h>

/*
  1、tasklet 是一种特殊的软中断，一般用在中断下半部
  2、tasklet 绑定的函数同一时间只能在一个 CPU 上运行，所以在SMP系统中不会引起并发的问题（每一个CPU都维护了一个tasklet）
  3、tasklet 绑定的函数中不能有休眠的函数，不然会引起系统异常
 */

struct tasklet_driver {
     int gpio;
     int irq;
     struct tasklet_struct tasklet;
};

static struct tasklet_driver td;

// // tasklet 静态初始化方法：
// static void tasklet_func(unsigned long data);
// DECLARE_TASKLET(tasklet_test, tasklet_func, 1024);

static void tasklet_func(unsigned long data)
{
    printk("tasklet_func, data = %lu\n", data);
}

static irqreturn_t test_handler(int irq, void *dev_id)
{
     printk("test_handler triggered, irq = %d\n", td.irq);
     // tasklet_schedule(&tasklet_test);
     tasklet_schedule(&td.tasklet);
     return IRQ_RETVAL(IRQ_HANDLED);
}

static int __init tasklet_driver_init(void)
{
     printk("tasklet_driver init\n");

     td.gpio = 137;
     td.irq = gpio_to_irq(td.gpio);
     printk("========> gpio = %d, irq = %d\n", td.gpio, td.irq);
                                            // 上升沿触发
     if (request_irq(td.irq, test_handler, IRQF_TRIGGER_RISING, "interrupt_test", NULL) < 0) {
        printk("request_irq error\n");
        return -EINVAL;
     }

//  动态初始化
     tasklet_init(&td.tasklet, tasklet_func, 1024);

     return 0;
}

static void __exit tasklet_driver_exit(void)
{
     printk("tasklet_driver Exit\n");
     free_irq(td.irq, NULL);
     // tasklet_kill(&tasklet_test);
     tasklet_kill(&td.tasklet);
}

module_init(tasklet_driver_init);
module_exit(tasklet_driver_exit);

MODULE_LICENSE("GPL");
