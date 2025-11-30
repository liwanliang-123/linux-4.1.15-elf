#include <linux/module.h>
#include <linux/init.h>

#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/delay.h>

/*
     中断线程话：
          中断线程话仍然可以将中断分成上半部和下半部，只是会将下半部交给一个专门的内核线程来处理
          这个内核线程只用于这个中断，当中断发生的时候会唤醒这个内核线程，然后由这个线程执行下半部
*/

struct interrupt_thread_driver {
     int gpio;
     int irq;
};

static struct interrupt_thread_driver itd;

static irqreturn_t interrupt_thread_handler_func(int irq, void *dev_id)
{
    printk("interrupt_thread_handler_func+\n");
    msleep(2000);
    printk("interrupt_thread_handler_func-\n");
    return IRQ_RETVAL(IRQ_HANDLED);
}

static irqreturn_t interrupt_handler_func(int irq, void *dev_id)
{
     printk("test_handler triggered, irq = %d\n", itd.irq);

     return IRQ_WAKE_THREAD;    // 唤醒中断线程 interrupt_thread_handler_func
}

static int __init interrupt_thread_driver_init(void)
{
     printk("interrupt_thread_driver_init init\n");

     itd.gpio = 137;
     itd.irq = gpio_to_irq(itd.gpio);
     printk("========> gpio = %d, irq = %d\n", itd.gpio, itd.irq);
                                            // 上升沿触发
     if (request_threaded_irq(itd.irq, interrupt_handler_func, interrupt_thread_handler_func, 
                                             IRQF_TRIGGER_RISING, "interrupt_test", NULL) < 0) {
        printk("request_irq error\n");
        return -EINVAL;
     }

     return 0;
}

static void __exit interrupt_thread_driver_exit(void)
{
     printk("interrupt_thread_driver_exit Exit\n");
}

module_init(interrupt_thread_driver_init);
module_exit(interrupt_thread_driver_exit);

MODULE_LICENSE("GPL");
