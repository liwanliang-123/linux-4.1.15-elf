#include <linux/init.h>
#include <linux/module.h>

static int __init hello_init(void)
{
# ifndef DEBUG1
     printk(KERN_INFO "Hello World Enter - A\n");
# else
     printk(KERN_INFO "Hello World Enter - B\n");
     printk(KERN_INFO "DEBUG value is %d\n", DEBUG1);
#endif

# ifdef DEBUG2
     printk(KERN_INFO "Hello World Enter - C\n");
# endif
     return 0;
}

static void __exit hello_exit(void)
{
     printk(KERN_INFO "Hello World Exit\n");
}

module_init(hello_init);
module_exit(hello_exit);
 
MODULE_LICENSE("GPL");
