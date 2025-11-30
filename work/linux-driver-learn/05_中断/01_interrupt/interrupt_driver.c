#include <linux/module.h>
#include <linux/init.h>

#include <linux/interrupt.h>
#include <linux/gpio.h>

/*
 *
 *  注意：需要先将 dts 中的 GPIO5_2、GPIO5_4、GPIO5_9 先注释掉，不然注册中断会失败
 * 
*/

// request_irq 是如何注册中断的？
//   1、request_irq 函数会初始化 irq action 
//   2、将 irq action 添加到 irq desc 中断描述符里面
//   3、当发生中断的时候，CPU 就可以通过中断描述符找到对应的 irq action,即找到对应的中断了，执行相应的 handler
//   每个硬件中断线都有一个 irq desc，可以通过 irq_to_desc 将该中断的中断号找到 irq desc

// 简化的关系示意图
//   struct irq_desc (每个中断线一个)  --has-->  struct irqaction (每个中断处理程序一个)
//      ↑                                     ↑
//      |                                     |
//   中断控制器管理                           具体设备的中断处理

//   每个中断描述符包含一个或多个中断动作
//   struct irq_desc {
//       // ...
//       struct irqaction *action;  // 链表头指针
//       // ...
//   };
// 中断处理流程： 
//    硬件中断 → 中断控制器 → 内核通用中断处理 → irq_desc.handle_irq() → 遍历irq_desc.action链表 → 调用每个irqaction.handler()


static int gpio, irq;

static irqreturn_t test_handler(int irq, void *dev_id)
{
    printk("test_handler triggered, irq = %d\n", irq);
    dump_stack();
    return IRQ_RETVAL(IRQ_HANDLED);
}

static int __init interrupt_driver_init(void)
{
    printk("interrupt_driver init\n");
/*
RK3568 GPIO编号计算方式：
     RK3568 有5组 GPIO：GPIO0~GPIO4，每组以 A0~A7,B0~B7,C0~C7,D0~D7作为编号进行区分

     引脚计算公式如下：
     GPIO Pin脚计算方式：pin = bank * 32 + number
     GPIO 小组编号计算方式：number = group * 8 + X

eg:  GPIO0_B5       (A0~A7,B0~B7,C0~C7,D0~D7 分别代表数字 0、1、2、3进行计算)
     number = (group = B,即等于1) 1 * 8 + 5（X就是代表B后面那个数字）
     pin = (bank = GPIO0中的0) 0 * 32 + number
     pin = 13

但是在 elfboard1 中的计算方式是为：GPIO编号 = (GPIO组号 - 1） × 每组的GPIO数量 + 引脚偏移量
     GPIO5_2:(5 - 1) x 32 + 2 = 130 
     GPIO5_4:(5 - 1) × 32 + 4 = 132
     GPIO5_9:(5 - 1) x 32 + 9 = 137
*/

    gpio = 137;
    irq = gpio_to_irq(gpio);
    printk("========> gpio = %d, irq = %d\n", gpio, irq);
                                        // 上升沿触发
    if (request_irq(irq, test_handler, IRQF_TRIGGER_RISING, "interrupt_test", NULL) < 0) {
        printk("request_irq error\n");
        return -EINVAL;
     }

    return 0;
}

static void __exit interrupt_driver_exit(void)
{
     printk("interrupt_driver Exit\n");
     free_irq(irq, NULL);
}

module_init(interrupt_driver_init);
module_exit(interrupt_driver_exit);

MODULE_LICENSE("GPL");
