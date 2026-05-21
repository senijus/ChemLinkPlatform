#include <linux/init.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
#include <linux/spi/spi.h>


static struct spi_device *padxl345_device;

static void adxl345_init(void)
{ 
    int ret = 0;
    char sendbuff[7] = {0};
    char recvbuff[7] = {0};

    sendbuff[0] = 0x00 | 0x80;
    ret = spi_write_then_read(padxl345_device, sendbuff, 1, recvbuff, 1);
    if(ret != 0) {
        pr_info("Kernel:adxl345 init fail\n");
        return;
    }
    pr_info("adlx345 ID : %#x\n", recvbuff[0]);

    sendbuff[0] = 0x31;
    sendbuff[1] = 0x08;
    ret = spi_write_then_read(padxl345_device, sendbuff, 2, NULL, 0);
    if(ret != 0) {
        pr_info("Kernel:adxl345 init fail\n");
        return;
    }

    sendbuff[0] = 0x2D;
    sendbuff[1] = 0x08;
    ret = spi_write_then_read(padxl345_device, sendbuff, 2, NULL, 0);
    if(ret != 0) {
        pr_info("Kernel:adxl345 init fail\n");
        return;
    }

    return;
}

static int adxl345_readdata(short *x, short *y, short *z)
{
    struct spi_message msg;
    char sendbuff[7] = {0};
    char recvbuff[7] = {0};
    struct spi_transfer xfer= {
        .tx_buf = sendbuff,
        .rx_buf = recvbuff,
        .len = 7,
        .delay_usecs = 20,
    };

    sendbuff[0] = 0x32 | 0x80 | 0x40;
    spi_message_init(&msg);
    spi_message_add_tail(&xfer, &msg);
    spi_sync(padxl345_device, &msg);

    *x = (recvbuff[2] << 8) | recvbuff[1];
    *y = (recvbuff[4] << 8) | recvbuff[3];
    *z = (recvbuff[6] << 8) | recvbuff[5];

    return 0;
}

static ssize_t adxl345_read(struct file *fp, char __user *puser, size_t n, loff_t *off)
{
    unsigned long nret = 0;
    short a[3] = {0};

    adxl345_readdata(&a[0], &a[1], &a[2]);

    nret = copy_to_user(puser, a, sizeof(a));
    if(nret != 0){
        pr_info("copy_to_user error\n");
    }

    return sizeof(a);
}


static struct file_operations fops = {
    .owner = THIS_MODULE,
    .read = adxl345_read,
};

static struct miscdevice adxl345_misc = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "adxl345_misc",
    .fops = &fops,
};

static int adxl345_probe(struct spi_device *spi)
{
    int ret = 0;

    padxl345_device = spi;

    ret = misc_register(&adxl345_misc);
    if (ret != 0) {
        pr_info("misc register faiadxl345\n");
        goto err_mis_register;
    }

    pr_info("Kernel:adxl345 probe success\n");

    adxl345_init();

    return 0;

err_mis_register:
    misc_deregister(&adxl345_misc);

    return -1;
}

static int adxl345_remove(struct spi_device *spi)
{
    misc_deregister(&adxl345_misc);

    pr_info("Kernel:adxl345 remove success\n");

    return 0;
}


static const struct of_device_id adxl345_of_match_table[] = {
    {
        .compatible = "pute,puteadxl345",
    },
    {},
};

static const struct spi_device_id adxl345_id_table[] = {
    {
        .name = "puteadxl345",
    },
    {},
};

static struct spi_driver adxl345_drv = {
    .probe = adxl345_probe,
    .remove = adxl345_remove,
    .driver = {
        .name = "puteadxl345",
        .of_match_table = adxl345_of_match_table,
    },
    .id_table = adxl345_id_table,
};

static int __init adxl345_drv_init(void)
{
    spi_register_driver(&adxl345_drv);

    pr_info("adxl345_drv_init success!\n");

    return 0;
}

static void __exit adxl345_drv_exit(void)
{
    spi_unregister_driver(&adxl345_drv);

    pr_info("adxl345_drv_exit success!\n");

    return;
}

module_init(adxl345_drv_init);
module_exit(adxl345_drv_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("pute");





