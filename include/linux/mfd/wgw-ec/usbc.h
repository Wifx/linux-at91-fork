/*
  * USB Type-C support driver for the Wifx board EC
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
#ifndef __LINUX_MFD_WGW_EC_USBC_H
#define __LINUX_MFD_WGW_EC_USBC_H

#include <linux/of.h>
#include <linux/notifier.h>
#include <linux/usb/role.h>
#include <linux/usb/typec.h>

#include <linux/mfd/wgw-ec/core.h>

enum usb_power_mode {
	USB_POWER_MODE_DETACHED,
	USB_POWER_MODE_SOURCE,
	USB_POWER_MODE_SINK,
	USB_POWER_MODE_ERROR,
};

enum usb_data_mode {
	USB_DATA_MODE_DEVICE,
	USB_DATA_MODE_HOST,
	USB_DATA_MODE_ERROR,
};

struct wgw_ec_usbc_dev {
	struct device *dev;
	struct wgw_ec_dev *ec;

	// notification from wgw-ec-dev
	struct notifier_block notifier;

	// keep track of all wgw-ec-usbc-dev
	struct list_head list_node;

	struct typec_port *port;
	struct typec_partner *partner;
	struct typec_capability typec_cap;
	struct usb_role_switch *role_sw;

	// cache
	spinlock_t lock;
	enum usb_data_mode data_mode;
	enum usb_power_mode power_mode;
};

/* Events and notification from the usb core */
#define WGW_USBC_DEVICE_PROBE		0x0001
#define WGW_USBC_NOTIFIER_UPDATE	0x0002
#define WGW_USBC_DATA_MODE_CHANGE	0x0003
#define WGW_USBC_POWER_MODE_CHANGE	0x0004

struct wgw_ec_usbc_notification {
	struct device *dev;
	enum usb_power_mode power_mode;
	enum usb_data_mode data_mode;
};

int wgw_ec_usbc_register_notifier(struct notifier_block *nb);
int wgw_ec_usbc_unregister_notifier(struct notifier_block *nb);

#endif /* __LINUX_MFD_WGW_EC_USBC_H */
