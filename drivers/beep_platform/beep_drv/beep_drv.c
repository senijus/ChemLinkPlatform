#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include "beep_drv.h"
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>

static beep_drv_t *pbeepcfg; 

static ssize_t beep_show(struct device *dev, struct device_attribute *attr, char *buf)
{   
    mutex_lock(&pbeepcfg->lock);
    if (BEEP_ON == pbeepcfg->curbeepstat) {
        pr_info("BEEP_ON\n");
    }
    else if (BEEP_OFF == pbeepcfg->curbeepstat) {
        pr_info("BEEP_OFF\n");
    }
    mutex_unlock(&pbeepcfg->lock);

    return 0;
}

static ssize_t beep_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    char tmpbuff[32] = {0};

    sscanf(buf, "%s", tmpbuff);

    mutex_lock(&pbeepcfg->lock);
    if (0 == strcmp(tmpbuff, "BEEP_ON")) {
        gpio_set_value(pbeepcfg->gpiono, 0);
        pbeepcfg->curbeepstat = BEEP_ON;
    } else if (0 == strcmp(tmpbuff, "BEEP_OFF")) {
        gpio_set_value(pbeepcfg->gpiono, 1);
        pbeepcfg->curbeepstat = BEEP_OFF;
    }
    mutex_unlock(&pbeepcfg->lock);

    return count;
}

static struct device_attribute beep_attr = {
    .attr = {
        .name = "attr",
        .mode = 0664,
    },
    .show = beep_show,
    .store = beep_store,
};

static int beep_open(struct inode *node, struct file *fp)
{
    pr_info("Kernel:beep open success\n");

    return 0;
}

static int beep_close(struct inode *node, struct file *fp)
{
    pr_info("Kernel:beep close success\n");

    return 0;
}

static ssize_t beep_read(struct file *fp, char __user *puser, size_t n, loff_t *off)
{
    unsigned long nret = 0;
    
    nret = copy_to_user(puser, &pbeepcfg->curbeepstat, 4);
    if (nret != 0) {
        pr_info("copy_to_user faibeep\n");
        return -1;
    }

    pr_info("Kernel:beep read success\n");

    return 0;
}

static ssize_t beep_write(struct file *fp, const char __user *puser, size_t n, loff_t *off)
{
    int setstat = 0;
    unsigned long nret = 0;
    
    nret = copy_from_user(&setstat, puser, 4);
    if (nret != 0) {
        pr_info("copy_from_user faibeep\n");
        return -1;
    }

    mutex_lock(&pbeepcfg->lock);
    if (BEEP_ON == setstat) {
        gpio_set_value(pbeepcfg->gpiono, 0);
        pbeepcfg->curbeepstat = BEEP_ON;
    }
    else if (BEEP_OFF == setstat) {
        gpio_set_value(pbeepcfg->gpiono, 1);
        pbeepcfg->curbeepstat = BEEP_OFF;
    }
    mutex_unlock(&pbeepcfg->lock);

    pr_info("Kernel:beep write success\n");

    return 0;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = beep_open,
    .release = beep_close,
    .read = beep_read,
    .write = beep_write,
};

static struct miscdevice beep_misc = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "beep_misc",
    .fops = &fops,
};


static int beep_probe(struct platform_device *pdevice)
{
    int ret = 0;

    //1.构建beep信息的空间
    pbeepcfg = kmalloc(sizeof(*pbeepcfg), GFP_KERNEL);
    if (NULL == pbeepcfg) {
        pr_info("kmalloc pbeepcfg faibeep\n");
        goto err_kmalloc;
    }

    //2.构建对设备操作的方法
    pbeepcfg->pbeepmisc = &beep_misc;

    //3.注册混杂设备
    ret = misc_register(&beep_misc);
    if (ret != 0) {
        pr_info("misc register faibeep\n");
        goto err_mis_register;
    }

    //3.初始化锁资源
    mutex_init(&pbeepcfg->lock);

    //4.找到设备树中的putebeep节点
    pbeepcfg->pnode = of_find_node_by_path("/putebeep");
    if (NULL == pbeepcfg->pnode) {
        pr_info("of_find_node_by_path faibeep\n");
        goto err_find_resource;
    }

    //5.获取节点中gpio-beep属性对应的GPIO编号
    pbeepcfg->gpiono = of_get_named_gpio(pbeepcfg->pnode, "gpio-beep", 0);
    if (pbeepcfg->gpiono < 0) {
        pr_info("get gpio-beep faibeep\n");
        goto err_find_resource;
    }

    //6.注册gpio的使用权
    ret = devm_gpio_request(beep_misc.this_device, pbeepcfg->gpiono, "beep_drv");
    if (ret != 0) {
        pr_info("devm_gpio_request faibeep\n");
        goto err_find_resource;
    }

    //7.设置gpio输出高电平
    gpio_direction_output(pbeepcfg->gpiono, 1);
    pbeepcfg->curbeepstat = BEEP_OFF;

    //8.增加sys调节节点
    ret = device_create_file(beep_misc.this_device, &beep_attr);
    if (ret != 0) {
        pr_info("device_create_file faibeep\n");
        return -1;
    }

    pr_info("major:%d, minor:%d\n", MAJOR(beep_misc.this_device->devt), MINOR(beep_misc.this_device->devt));

    pr_info("Kernel:beep probe success\n");

    return 0;

err_find_resource:
    misc_deregister(&beep_misc);
err_mis_register:
    kfree(pbeepcfg);
err_kmalloc:
    return -1;
}

static int beep_remove(struct platform_device *pdevice)
{
    device_remove_file(beep_misc.this_device, &beep_attr);

    mutex_destroy(&pbeepcfg->lock);

    misc_deregister(&beep_misc);

    kfree(pbeepcfg);

    pr_info("Kernel:beep remove success\n");

    return 0;
}

static const struct of_device_id beep_of_match_table[] = {
    {
        .compatible = "pute,putebeep",
    },
    {},
};

static const struct platform_device_id beep_id_table[] = {
    {
        .name = "putebeep",
    },
    {},
};

static struct platform_driver beep_drv = {
    .probe = beep_probe,
    .remove = beep_remove,
    .driver = {
        .name = "putebeep",
        .of_match_table = beep_of_match_table,
    },
    .id_table = beep_id_table,
};

static int __init beep_drv_init(void)
{
    platform_driver_register(&beep_drv);

    pr_info("beep_drv_init success!\n");

    return 0;
}

static void __exit beep_drv_exit(void)
{
    platform_driver_unregister(&beep_drv);

    pr_info("beep_drv_exit success!\n");

    return;
}

module_init(beep_drv_init);
module_exit(beep_drv_exit);

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
