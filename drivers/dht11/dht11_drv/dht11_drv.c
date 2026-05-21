#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <asm/delay.h>
#include <linux/spinlock.h>

static int gpiono = 0;
static spinlock_t lock;

extern void msleep(unsigned int msecs);

static void dht11_init(void)
{
    gpio_direction_output(gpiono, 1);

    gpio_set_value(gpiono, 0);
    msleep(20);

    gpio_set_value(gpiono, 1);
    

    return;
}

static int dht11_reply(void)
{
    int cnt = 0;

    gpio_direction_input(gpiono);

    while (gpio_get_value(gpiono) == 1) {
        if (cnt++ > 10) {
            pr_info("dht11 no respond timeout\n");
            return -1;
        }
        udelay(10);
    }

    while(gpio_get_value(gpiono) == 0);

    return 0;
}

static int dht11_read_data(unsigned char *pdata, int datalen) 
{ 
    int i = 0;
    int cnt = 0;
    int j = 0;

    while(gpio_get_value(gpiono) == 1);

    for(j = 0; j < datalen; j++) {
        for(i = 7; i >= 0; i--) {
            cnt = 0;
            while(gpio_get_value(gpiono) == 0);
            while(gpio_get_value(gpiono) == 1) {
                cnt++;
                udelay(10);
            }
            if(cnt > 5) {
                pdata[j] |= (1 << i); 
            }

        }
    }

    return 0;
}

static int dht11_data_check(unsigned char *pdata, int datalen)
{
    int i = 0;
    int sum = 0;
    
    for(i = 0; i < datalen - 1; i++) {
        sum += pdata[i];
    }

    if(sum == pdata[4]) {
        return 0;
    }
    return -1;
}

static ssize_t dht11_read(struct file *fp, char __user *puser, size_t n, loff_t *off)
{
    unsigned long nret = 0;
    unsigned char data[5] = {0};
    int ret = 0;
    unsigned long flag = 0;

    dht11_init();

    spin_lock_irqsave(&lock, flag);
    ret = dht11_reply();
    if (ret < 0) {
        pr_info("dht11 reply timeout\n");
        return -1;
    }

    dht11_read_data(data, sizeof(data));
    spin_unlock_irqrestore(&lock, flag);

    ret = dht11_data_check(data, sizeof(data));
    if (ret < 0) {
        pr_info("dht11 data check error\n");
        return -1;
    }
    
    nret = copy_to_user(puser, &data, sizeof(data));
    if (nret != 0) {
        pr_info("copy_to_user faidht11\n");
        return -1;
    }

    pr_info("Kernel:dht11 read success\n");

    return sizeof(data);
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .read = dht11_read,
};

static struct miscdevice dht11_misc = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "dht11_misc",
    .fops = &fops,
};


static int dht11_probe(struct platform_device *pdevice)
{
    int ret = 0;

    //1.注册混杂设备
    ret = misc_register(&dht11_misc);
    if (ret != 0) {
        pr_info("misc register faidht11\n");
        goto err_mis_register;
    }

    //2.获取节点中gpio-dht11属性对应的GPIO编号
    gpiono = of_get_named_gpio(pdevice->dev.of_node, "gpio-dht11", 0);
    if (gpiono < 0) {
        pr_info("get gpio-dht11 faidht11\n");
        goto err_find_resource;
    }

    //3.注册gpio的使用权
    ret = devm_gpio_request(dht11_misc.this_device, gpiono, "dht11_drv");
    if (ret != 0) {
        pr_info("devm_gpio_request faidht11\n");
        goto err_find_resource;
    }

    //4.设置gpio输出高电平
    gpio_direction_output(gpiono, 1);

    //5初始化锁
    spin_lock_init(&lock);

    pr_info("major:%d, minor:%d\n", MAJOR(dht11_misc.this_device->devt), MINOR(dht11_misc.this_device->devt));

    pr_info("Kernel:dht11 probe success\n");

    return 0;

err_find_resource:
    misc_deregister(&dht11_misc);
err_mis_register:
    return -1;
}

static int dht11_remove(struct platform_device *pdevice)
{
    misc_deregister(&dht11_misc);

    pr_info("Kernel:dht11 remove success\n");

    return 0;
}

static const struct of_device_id dht11_of_match_table[] = {
    {
        .compatible = "pute,putedht11",
    },
    {},
};

static const struct platform_device_id dht11_id_table[] = {
    {
        .name = "putedht11",
    },
    {},
};

static struct platform_driver dht11_drv = {
    .probe = dht11_probe,
    .remove = dht11_remove,
    .driver = {
        .name = "putedht11",
        .of_match_table = dht11_of_match_table,
    },
    .id_table = dht11_id_table,
};

static int __init dht11_drv_init(void)
{
    platform_driver_register(&dht11_drv);

    pr_info("dht11_drv_init success!\n");

    return 0;
}

static void __exit dht11_drv_exit(void)
{
    platform_driver_unregister(&dht11_drv);

    pr_info("dht11_drv_exit success!\n");

    return;
}

module_init(dht11_drv_init);
module_exit(dht11_drv_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("pute");


