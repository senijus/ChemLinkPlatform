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
#include <linux/i2c.h>


static struct i2c_client *plm75a_client;

static ssize_t lm75a_read(struct file *fp, char __user *puser, size_t n, loff_t *off)
{
    struct i2c_msg sendmsg;
    struct i2c_msg recvmsg;
    unsigned long nret = 0;
    unsigned char tmpbuff[2] = {0};
    unsigned short tmpval = 0;

    memset(&sendmsg, 0, sizeof(sendmsg));
    memset(&recvmsg, 0, sizeof(recvmsg));

    sendmsg.addr = plm75a_client->addr;
    tmpbuff[0] = 0x00;
    sendmsg.buf = tmpbuff;
    sendmsg.len = 1;
    plm75a_client->adapter->algo->master_xfer(plm75a_client->adapter, &sendmsg, 1);

    recvmsg.addr = plm75a_client->addr;
    recvmsg.flags |= I2C_M_RD;
    recvmsg.buf = tmpbuff;
    recvmsg.len = 2;
    plm75a_client->adapter->algo->master_xfer(plm75a_client->adapter, &recvmsg, 1);

    tmpval = ((tmpbuff[0] << 8 | tmpbuff[1]) >> 7);
    nret = copy_to_user(puser, &tmpval, 2);
    if (nret != 0) {
        pr_info("copy_to_user failm75a\n");
        return -1;
    }

    pr_info("Kernel:lm75a read success\n");

    return sizeof(tmpval);
}


static struct file_operations fops = {
    .owner = THIS_MODULE,
    .read = lm75a_read,
};

static struct miscdevice lm75a_misc = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "lm75a_misc",
    .fops = &fops,
};

static int lm75a_probe(struct i2c_client *pclient, const struct i2c_device_id *pid)
{
    int ret = 0;

    plm75a_client = pclient;

    ret = misc_register(&lm75a_misc);
    if (ret != 0) {
        pr_info("misc register failm75a\n");
        goto err_mis_register;
    }

    plm75a_client = pclient;

    pr_info("Kernel:lm75a probe success\n");

    return 0;

err_mis_register:
    misc_deregister(&lm75a_misc);

    return -1;
}

static int lm75a_remove(struct i2c_client *pdevice)
{
    misc_deregister(&lm75a_misc);

    pr_info("Kernel:lm75a remove success\n");

    return 0;
}


static const struct of_device_id lm75a_of_match_table[] = {
    {
        .compatible = "pute,putelm75a",
    },
    {},
};

static const struct i2c_device_id lm75a_id_table[] = {
    {
        .name = "putelm75a",
    },
    {},
};

static struct i2c_driver lm75a_drv = {
    .probe = lm75a_probe,
    .remove = lm75a_remove,
    .driver = {
        .name = "putelm75a",
        .of_match_table = lm75a_of_match_table,
    },
    .id_table = lm75a_id_table,
};

static int __init lm75a_drv_init(void)
{
    i2c_add_driver(&lm75a_drv);

    pr_info("lm75a_drv_init success!\n");

    return 0;
}

static void __exit lm75a_drv_exit(void)
{
    i2c_del_driver(&lm75a_drv);

    pr_info("lm75a_drv_exit success!\n");

    return;
}

module_init(lm75a_drv_init);
module_exit(lm75a_drv_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("pute");





