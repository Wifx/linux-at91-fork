/*
 * Miscellaneous character driver for Wifx board EC
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
#ifndef __LINUX_MFD_WGW_EC_CHARDEV_H
#define __LINUX_MFD_WGW_EC_CHARDEV_H

#include <linux/i2c-dev.h>

#define WGW_EC_DEV_IOC 0xEC
#define WGW_EC_DEV_IOC_PKT_CMD                                                 \
	_IOW(WGW_EC_DEV_IOC, 1, struct i2c_smbus_ioctl_data)
#define WGW_EC_DEV_IOC_PKT_CMD_LTR                                             \
	_IOW(WGW_EC_DEV_IOC, 2, struct i2c_smbus_ioctl_data)

#endif /* __LINUX_MFD_WGW_EC_CHARDEV_H */
