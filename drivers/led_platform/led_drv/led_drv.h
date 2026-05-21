#ifndef __LED_DRV_H__
#define __LED_DRV_H__

#define LED_ON  1
#define LED_OFF 0

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/miscdevice.h>
#include <linux/of.h>

typedef struct led_drv {
    struct miscdevice *pledmisc;
    int curledstat;
    struct mutex lock;
    struct device_node *pnode;
    int gpiono;
}led_drv_t;

#endif