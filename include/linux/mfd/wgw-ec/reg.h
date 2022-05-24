/*
 * Register information for the Wifx board EC
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
#ifndef __LINUX_MFD_WGW_EC_REG_H
#define __LINUX_MFD_WGW_EC_REG_H

#define WGW_EC_REG_PROTOC_VER 0x00
#define WGW_EC_REG_HW_INFO 0x10
#define WGW_EC_REG_FW_INFO1 0x20
#define WGW_EC_REG_FW_INFO2 0x21
#define WGW_EC_REG_FW_INFO3 0x22
#define WGW_EC_REG_FW_INFO4 0x23

#define WGW_EC_REG_CMD_LTR_STATUS 0x30
#define WGW_EC_REG_MEM_SLOT0 0x31
#define WGW_EC_REG_MEM_SLOT0_CTRL 0x35

#define WGW_EC_REG_LED_START 0x60

#define WGW_EC_REG_LAST_RESET_STATE 0x70

#define WGW_EC_REG_USB_MODE_POWER 0x80
#define WGW_EC_REG_USB_MODE_DATA 0x81

#define WGW_EC_REG_INTERRUPT 0xA0
#define WGW_EC_REG_MAX 0xFF

#endif /* __LINUX_MFD_WGW_EC_REG_H */
