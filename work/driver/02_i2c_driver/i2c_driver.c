#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/fs.h>
#include <linux/device.h>

static struct i2c_client *my_client;

static int i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
     printk(KERN_INFO "i2c_probe enter\n");
     my_client = client;

     printk(KERN_INFO "client name = %s\n", client->name);
     printk(KERN_INFO "client addr = %x\n", client->addr);
     printk(KERN_INFO "client irq = %d\n", client->irq);

     return 0;
}

static int i2c_remove(struct i2c_client *client)
{
     printk(KERN_INFO "i2c_remove enter\n");
     return 0;
}

static const struct i2c_device_id my_i2c_dev_id[] = { 
     { "myi2c", 0 }
};

// static const struct of_device_id i2c_match_table[] = {
//      {.compatible = "myi2c", },
//      { },
// };

struct i2c_driver i2c_drv = {
     .driver = {
          .owner = THIS_MODULE,
          .name = "myi2c",
          // .of_match_table = i2c_match_table,
     },
     .id_table = my_i2c_dev_id,
     .probe = i2c_probe,
     .remove = i2c_remove,
};

static int __init i2c_driver_init(void)
{
     printk(KERN_INFO "i2c driver enter\n");
     return i2c_add_driver(&i2c_drv);
}

static void __exit i2c_driver_exit(void)
{
     printk(KERN_INFO "i2c driver exit\n");
     i2c_del_driver(&i2c_drv);
}

module_init(i2c_driver_init);
module_exit(i2c_driver_exit)
MODULE_LICENSE("GPL");