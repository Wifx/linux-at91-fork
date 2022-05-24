/*
 * MFD driver core for the Wifx board Embedded Controller
 *
 *  Copyright (C) 2021 Wifx,
 *                2021 Yannick Lanz <yannick.lanz@wifx.net>
 *
 * This file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef __LINUX_MFD_WGW_EC_CORE_H
#define __LINUX_MFD_WGW_EC_CORE_H

#include <linux/kernel.h>
#include <linux/ioctl.h>
#include <linux/mfd/wgw-ec/reg.h>
#include <linux/platform_device.h>

#define WGW_EC_APP_COMMIT_HASH_SIZE 16
#define WGW_EC_APP_COMMIT_DATE_SIZE 32
#define WGW_EC_HW_VERSION_SIZE 32
#define WGW_EC_FW_VERSION_SIZE 32
#define WGW_EC_HW_SN_SIZE 16

#define WGW_EC_SERIAL_ERROR (-1)
#define WGW_EC_SERIAL_SET 0x01
#define WGW_EC_SERIAL_OTP 0x02
#define WGW_EC_SERIAL_SET_OTP (WGW_EC_SERIAL_SET | WGW_EC_SERIAL_OTP)

struct wgw_ec_serial {
	char data[WGW_EC_HW_SN_SIZE];
	u8 state;
};

enum wgw_ec_model {
	WGW_EC_M_LORIX_ONE = 0,
	WGW_EC_M_WIFX_L1 = 1,
	WGW_EC_M_WIFX_Y1 = 2,
};

enum wgw_ec_variant {
	WGW_EC_V_STANDARD = 0,
};

enum wgw_ec_frequency {
	WGW_EC_F_863_870 = 0,
	WGW_EC_F_902_928 = 1,
};

struct wgw_ec_version {
	u16 major;
	u16 minor;
	u16 revision;
};

struct wgw_ec_hw_tuple_info {
	u8 id;
	const char *str;
};

struct wgw_ec_hw_info {
	struct wgw_ec_version version;
	char version_str[WGW_EC_HW_VERSION_SIZE];
	struct wgw_ec_hw_tuple_info model;
	struct wgw_ec_hw_tuple_info variant;
	struct wgw_ec_hw_tuple_info frequency;
};

struct wgw_ec_fw_info {
	struct wgw_ec_version version;
	char version_str[WGW_EC_FW_VERSION_SIZE];
	char commit_hash[WGW_EC_APP_COMMIT_HASH_SIZE];
	char commit_date[WGW_EC_APP_COMMIT_DATE_SIZE];
};

struct wgw_ec_info {
	struct wgw_ec_fw_info fw_info;
	struct wgw_ec_hw_info hw_info;
	struct wgw_ec_serial serial;
	u8 boot_state;
	u8 protoc;
};

#define WGW_EC_MEM_SLOT_SIZE 32
struct wgw_ec_memory_slot {
	u8 data[WGW_EC_MEM_SLOT_SIZE];
	u8 length;
	u8 flags;
};

struct wgw_ec_device {
	const char *phys_name;
	struct device *dev;
	void *priv;

	int irq;
	struct gpio_desc *cpu_state_pin;
	struct mutex lock_ltr;

	struct blocking_notifier_head notifier_list;

	// Returns length read (>= 0) or error (< 0)
	int (*read_byte)(struct wgw_ec_device *mcu, char command, u8 *data);
	int (*read_word)(struct wgw_ec_device *mcu, char command, u16 *data);
	int (*read_block)(struct wgw_ec_device *mcu, char command, u8 *data);

	// Returns length written (>= 0) or error (< 0)
	int (*write_byte)(struct wgw_ec_device *mcu, char command, u8 data);
	int (*write_word)(struct wgw_ec_device *mcu, char command, u16 data);
	int (*write_block)(struct wgw_ec_device *mcu, char command,
			   const u8 *data, u8 len);

	/* The platform devices used by the mfd driver */
	struct platform_device *ec;
};

struct wgw_ec_dev {
	struct device class_dev;
	struct wgw_ec_device *ec_dev;
	struct device *dev;
	struct wgw_ec_info cache_info;
	struct mutex cache_lock;
};

extern int wgw_ec_register(struct wgw_ec_device *wgw_dev);
extern int wgw_ec_unregister(struct wgw_ec_device *wgw_dev);

extern int wgw_ec_get_ltr_status(struct wgw_ec_dev *ec);
extern int wgw_ec_wait_ready(struct wgw_ec_dev *ec);

extern int wgw_ec_serial_set(struct wgw_ec_dev *ec, char *serial,
			     struct wgw_ec_serial *serial_cache);

extern int wgw_ec_boot_state_get(struct wgw_ec_dev *ec, u8 *boot_state);
extern int wgw_ec_boot_state_clr_update(struct wgw_ec_dev *ec, u8 *boot_state);

#define to_wgw_ec_dev(dev) container_of(dev, struct wgw_ec_dev, class_dev)

#endif /* __LINUX_MFD_WGW_EC_CORE_H */
