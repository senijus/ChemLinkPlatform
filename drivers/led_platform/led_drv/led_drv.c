#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include "led_drv.h"
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>

static led_drv_t *pledcfg; 

static ssize_t led_show(struct device *dev, struct device_attribute *attr, char *buf)
{   
    mutex_lock(&pledcfg->lock);
    if (LED_ON == pledcfg->curledstat) {
        pr_info("LED_ON\n");
    }
    else if (LED_OFF == pledcfg->curledstat) {
        pr_info("LED_OFF\n");
    }
    mutex_unlock(&pledcfg->lock);

    return 0;
}

static ssize_t led_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    char tmpbuff[32] = {0};

    sscanf(buf, "%s", tmpbuff);

    mutex_lock(&pledcfg->lock);
    if (0 == strcmp(tmpbuff, "LED_ON")) {
        gpio_set_value(pledcfg->gpiono, 0);
        pledcfg->curledstat = LED_ON;
    } else if (0 == strcmp(tmpbuff, "LED_OFF")) {
        gpio_set_value(pledcfg->gpiono, 1);
        pledcfg->curledstat = LED_OFF;
    }
    mutex_unlock(&pledcfg->lock);

    return count;
}

static struct device_attribute led_attr = {
    .attr = {
        .name = "attr",
        .mode = 0664,
    },
    .show = led_show,
    .store = led_store,
};

static int led_open(struct inode *node, struct file *fp)
{
    pr_info("Kernel:led open success\n");

    return 0;
}

static int led_close(struct inode *node, struct file *fp)
{
    pr_info("Kernel:led close success\n");

    return 0;
}

static ssize_t led_read(struct file *fp, char __user *puser, size_t n, loff_t *off)
{
    unsigned long nret = 0;
    
    nret = copy_to_user(puser, &pledcfg->curledstat, 4);
    if (nret != 0) {
        pr_info("copy_to_user failed\n");
        return -1;
    }

    pr_info("Kernel:led read success\n");

    return 0;
}

static ssize_t led_write(struct file *fp, const char __user *puser, size_t n, loff_t *off)
{
    int setstat = 0;
    unsigned long nret = 0;
    
    nret = copy_from_user(&setstat, puser, 4);
    if (nret != 0) {
        pr_info("copy_from_user failed\n");
        return -1;
    }

    mutex_lock(&pledcfg->lock);
    if (LED_ON == setstat) {
        gpio_set_value(pledcfg->gpiono, 0);
        pledcfg->curledstat = LED_ON;
    }
    else if (LED_OFF == setstat) {
        gpio_set_value(pledcfg->gpiono, 1);
        pledcfg->curledstat = LED_OFF;
    }
    mutex_unlock(&pledcfg->lock);

    pr_info("Kernel:led write success\n");

    return 0;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = led_open,
    .release = led_close,
    .read = led_read,
    .write = led_write,
};

static struct miscdevice led_misc = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "led_misc",
    .fops = &fops,
};


static int led_probe(struct platform_device *pdevice)
{
    int ret = 0;

    //1.构建led信息的空间
    pledcfg = kmalloc(sizeof(*pledcfg), GFP_KERNEL);
    if (NULL == pledcfg) {
        pr_info("kmalloc pledcfg failed\n");
        goto err_kmalloc;
    }

    //2.构建对设备操作的方法
    pledcfg->pledmisc = &led_misc;

    //3.注册混杂设备
    ret = misc_register(&led_misc);
    if (ret != 0) {
        pr_info("misc register failed\n");
        goto err_mis_register;
    }

    //3.初始化锁资源
    mutex_init(&pledcfg->lock);

    //4.找到设备树中的puteled节点
    pledcfg->pnode = of_find_node_by_path("/puteled");
    if (NULL == pledcfg->pnode) {
        pr_info("of_find_node_by_path failed\n");
        goto err_find_resource;
    }

    //5.获取节点中gpio-led属性对应的GPIO编号
    pledcfg->gpiono = of_get_named_gpio(pledcfg->pnode, "gpio-led", 0);
    if (pledcfg->gpiono < 0) {
        pr_info("get gpio-led failed\n");
        goto err_find_resource;
    }

    //6.注册gpio的使用权
    ret = devm_gpio_request(led_misc.this_device, pledcfg->gpiono, "led_drv");
    if (ret != 0) {
        pr_info("devm_gpio_request failed\n");
        goto err_find_resource;
    }

    //7.设置gpio输出高电平
    gpio_direction_output(pledcfg->gpiono, 1);
    pledcfg->curledstat = LED_OFF;

    //8.增加sys调节节点
    ret = device_create_file(led_misc.this_device, &led_attr);
    if (ret != 0) {
        pr_info("device_create_file failed\n");
        return -1;
    }

    pr_info("major:%d, minor:%d\n", MAJOR(led_misc.this_device->devt), MINOR(led_misc.this_device->devt));

    pr_info("Kernel:led probe success\n");

    return 0;

err_find_resource:
    misc_deregister(&led_misc);
err_mis_register:
    kfree(pledcfg);
err_kmalloc:
    return -1;
}

static int led_remove(struct platform_device *pdevice)
{
    device_remove_file(led_misc.this_device, &led_attr);

    mutex_destroy(&pledcfg->lock);

    misc_deregister(&led_misc);

    kfree(pledcfg);

    pr_info("Kernel:led remove success\n");

    return 0;
}

static const struct of_device_id led_of_match_table[] = {
    {
        .compatible = "pute,puteled",
    },
    {},
};

static const struct platform_device_id led_id_table[] = {
    {
        .name = "puteled",
    },
    {},
};

static struct platform_driver led_drv = {
    .probe = led_probe,
    .remove = led_remove,
    .driver = {
        .name = "puteled",
        .of_match_table = led_of_match_table,
    },
    .id_table = led_id_table,
};

static int __init led_drv_init(void)
{
    platform_driver_register(&led_drv);

    pr_info("led_drv_init success!\n");

    return 0;
}

static void __exit led_drv_exit(void)
{
    platform_driver_unregister(&led_drv);

    pr_info("led_drv_exit success!\n");

    return;
}

module_init(led_drv_init);
module_exit(led_drv_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("pute");






//  struct platform_driver {                                                                                                                                                                                                                                
// 175     int (*probe)(struct platform_device *);
// 176     int (*remove)(struct platform_device *);
// 177     void (*shutdown)(struct platform_device *);
// 178     int (*suspend)(struct platform_device *, pm_message_t state);
// 179     int (*resume)(struct platform_device *);
// 180     struct device_driver driver;
// 181     const struct platform_device_id *id_table;
// 182     bool prevent_deferred_probe;
// 183 };

// struct platform_device {
//  23     const char  *name;
//  24     int     id; 
//  25     bool        id_auto;
//  26     struct device   dev;
//  27     u32     num_resources;
//  28     struct resource *resource;
//  29 
//  30     const struct platform_device_id *id_entry;
//  31     char *driver_override; /* Driver name to force a match */                                                                                                                                                                                           
//  32 
//  33     /* MFD cell pointer */
//  34     struct mfd_cell *mfd_cell;
//  35 
//  36     /* arch specific additions */
//  37     struct pdev_archdata    archdata;
//  38 };


// struct device_driver {                                                                                                                                                                                                                                 
//  231     const char      *name;
//  232     struct bus_type     *bus;
//  233 
//  234     struct module       *owner;
//  235     const char      *mod_name;  /* used for built-in modules */
//  236 
//  237     bool suppress_bind_attrs;   /* disables bind/unbind via sysfs */
//  238 
//  239     const struct of_device_id   *of_match_table;
//  240     const struct acpi_device_id *acpi_match_table;
//  241 
//  242     int (*probe) (struct device *dev);
//  243     int (*remove) (struct device *dev);
//  244     void (*shutdown) (struct device *dev);
//  245     int (*suspend) (struct device *dev, pm_message_t state);
//  246     int (*resume) (struct device *dev);
//  247     const struct attribute_group **groups;
//  248    
//  249     const struct dev_pm_ops *pm;
//  250    
//  251     struct driver_private *p;
//  252 }; 

//   struct of_device_id {                                                                                                                                                                                                                                   
// 224     char    name[32];
// 225     char    type[32];
// 226     char    compatible[128];
// 227     const void *data;
// 228 };
