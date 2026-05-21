OBJ := mqtt

CC := arm-linux-gnueabihf-gcc
LVGL_DIR_NAME ?= lvgl
LVGL_DIR ?= ${shell pwd}

# 内核源码路径（可通过命令行覆盖：make KERNDIR=/your/kernel/path）
KERNDIR ?= /home/linux/my_study/yinjian/qudong/reason/kenal/linux-imx-rel_imx_4.1.15_2.1.0_ga_alientek
export kerdir := $(KERNDIR)

# 驱动目录列表
DRIVERS := dht11 adxl345_spi beep_platform led_platform lm75a_i2c
DRIVERS_DIR := $(LVGL_DIR)/drivers

OBJEXT ?= .o

# LVGL sources (must be included before CFLAGS is finalized)
include $(LVGL_DIR)/lvgl/lvgl.mk
include $(LVGL_DIR)/lv_drivers/lv_drivers.mk

# Now append project flags to the CFLAGS set by lv_drivers.mk
override CFLAGS += -O3 -g0 -std=c99 \
	-Wall -Wshadow -Wundef -Wmissing-prototypes \
	-Wextra -Wno-unused-function -Wno-error=strict-prototypes \
	-Wpointer-arith -fno-strict-aliasing -Wno-error=cpp -Wuninitialized \
	-Wmaybe-uninitialized -Wno-unused-parameter -Wno-missing-field-initializers \
	-Wtype-limits -Wsizeof-pointer-memaccess -Wno-format-nonliteral -Wno-cast-qual \
	-Wunreachable-code -Wno-switch-default -Wreturn-type -Wmultichar -Wformat-security \
	-Wno-ignored-qualifiers -Wno-error=pedantic -Wno-sign-compare \
	-Wno-error=missing-prototypes -Wdouble-promotion \
	-I./include -I./config -I./lib/libmodbus/include

# Linker flags
LDFLAGS ?= -lm -lpthread -lpaho-mqtt3c -lrt -lsqlite3 -lssl -lcrypto -lmodbus
LIBS += -L./lib/paholib
LIBS += -L./lib/openssllib
LIBS += -L./lib/sqlitelib
LIBS += -L./lib/libmodbus/lib

# Project sources - core modules
PROJ_SRCS += src/core/log.c
PROJ_SRCS += src/core/main.c
PROJ_SRCS += src/core/mailbox.c
PROJ_SRCS += src/core/mqtt.c
PROJ_SRCS += src/core/storage.c
PROJ_SRCS += src/core/auth.c
PROJ_SRCS += src/core/alarm_mgr.c

# Project sources - UI
PROJ_SRCS += src/ui/lvgl_ui.c
PROJ_SRCS += src/ui/ui_common.c
PROJ_SRCS += src/ui/ui_login.c
PROJ_SRCS += src/ui/ui_dashboard.c
PROJ_SRCS += src/ui/ui_trend.c
PROJ_SRCS += src/ui/ui_alarm.c
PROJ_SRCS += src/ui/ui_settings.c

# Project sources - HAL
PROJ_SRCS += src/hal/sensor_hal.c

AOBJS = $(ASRCS:.S=$(OBJEXT))
COBJS = $(CSRCS:.c=$(OBJEXT))
PROJ_OBJS = $(PROJ_SRCS:.c=$(OBJEXT))

SRCS = $(ASRCS) $(CSRCS) $(PROJ_SRCS)
OBJS = $(AOBJS) $(COBJS) $(PROJ_OBJS)

all: default drivers

%.o: %.c
	@$(CC) $(CFLAGS) -c $< -o $@
	@echo "CC $<"

default: $(AOBJS) $(COBJS) $(PROJ_OBJS)
	$(CC) -o $(OBJ) $(AOBJS) $(COBJS) $(PROJ_OBJS) $(LDFLAGS) $(LIBS)
	cp $(OBJ) ~/nfs/rootfs/mqtt_test/

.PHONY: drivers clean_drivers distclean_drivers

# 编译所有内核驱动模块
drivers:
	@for drv in $(DRIVERS); do \
		echo "===== Building driver: $$drv ====="; \
		$(MAKE) -C $(DRIVERS_DIR)/$$drv; \
	done

clean: clean_drivers
	rm -f $(OBJ) $(AOBJS) $(COBJS) $(PROJ_OBJS)

clean_drivers:
	@for drv in $(DRIVERS); do \
		echo "===== Cleaning driver: $$drv ====="; \
		$(MAKE) -C $(DRIVERS_DIR)/$$drv clean; \
	done

distclean: distclean_drivers
	rm -f $(OBJ) $(AOBJS) $(COBJS) $(PROJ_OBJS)
	rm -f ~/nfs/rootfs/mqtt_test/$(OBJ)

distclean_drivers:
	@for drv in $(DRIVERS); do \
		echo "===== Distclean driver: $$drv ====="; \
		$(MAKE) -C $(DRIVERS_DIR)/$$drv distclean; \
	done
