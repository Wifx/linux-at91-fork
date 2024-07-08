/*
 * Multifunctionnal device driver for the Wifx board Embedded Controller
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
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/mfd/core.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <linux/mfd/wgw-ec/core.h>
#include <linux/mfd/wgw-ec/reg.h>

#define DRV_NAME "wgw-ec-dev"

static struct class wifx_class = {
	.owner = THIS_MODULE,
	.name = "wifx",
};

static struct mfd_cell wgw_ec_mfd_cells[] = {
	{
		.name = "wgw-ec-leds",
		.of_compatible = "wifx,wgw-ec-leds",
	},
	{
		.name = "wgw-ec-usbc",
		.of_compatible = "wifx,wgw-ec-usbc",
	},
};

static const struct mfd_cell wgw_ec_platform_cells[] = {
	{
		.name = "wgw-ec-chardev",
	},
	{
		.name = "wgw-ec-sysfs",
	},
};

#define WGW_EC_MEM_SLOT_EMPTY 0x00
#define WGW_EC_MEM_SLOT_SET 0x01
#define WGW_EC_MEM_SLOT_OTP 0x02
#define WGW_EC_MEM_SLOT_SET_OTP (WGW_EC_MEM_SLOT_SET | WGW_EC_MEM_SLOT_OTP)
#define WGW_EC_MEM_SLOT_STATE_Msk 0x03

struct hw_info {
	struct wgw_ec_version version;
	u8 model;
	u8 variant;
	u8 frequency;
};

struct wgw_ec_reg {
	union {
		u8 data[32];
		struct wgw_ec_version fw_info_version;
		const char *fw_info_commit_hash;
		const char *fw_info_commit_date;
		struct hw_info hw_info;
	};
};

static const char *unknown_str = "unknown";
static const char *undefined_str = "undefined";
static const char *error_str = "error";

static const char *mainboard_model_strs[] = { "wgw-l01-base", "wgw-l02-base",
					      "wgw-l02-base-y1",
					      "wgw-l02-base-4g" };
int mainboard_ref_index(enum wgw_ec_mainboard_ref mb_ref)
{
	switch (mb_ref) {
	case WGW_EC_MB_WGW_L01_BASE:
	case WGW_EC_MB_WGW_L02_BASE_L1:
	case WGW_EC_MB_WGW_L02_BASE_Y1:
	case WGW_EC_MB_WGW_L02_BASE_L1_4G:
		return (int)mb_ref;
	default:
		return -1;
	}
}
const char *mainboard_ref_str(enum wgw_ec_mainboard_ref mb_ref)
{
	int index;
	if ((index = mainboard_ref_index(mb_ref)) < 0) {
		return unknown_str;
	}
	return mainboard_model_strs[index];
}

static const char *mainboard_variant_strs[] = { "8XX", "9XX" };
int mainboard_variant_index(enum wgw_ec_mainboard_variant mb_variant)
{
	switch (mb_variant) {
	case WGW_EC_MB_VARIANT_8XX:
	case WGW_EC_MB_VARIANT_9XX:
		return (int)mb_variant;
	default:
		return -1;
	}
}
const char *mainboard_variant_str(enum wgw_ec_mainboard_variant mb_variant)
{
	int index;
	if ((index = mainboard_variant_index(mb_variant)) < 0) {
		return unknown_str;
	}
	return mainboard_variant_strs[index];
}

static const char *model_strs[] = { "lorix-one", "wifx-l1", "wifx-y1",
				    "wifx-l1-4g" };
static const char *model_pretty_strs[] = { "LORIX One", "Wifx L1", "Wifx Y1",
					   "Wifx L1 4G" };
int model_index(enum wgw_ec_model model)
{
	switch (model) {
	case WGW_EC_M_LORIX_ONE:
	case WGW_EC_M_WIFX_L1:
	case WGW_EC_M_WIFX_Y1:
	case WGW_EC_M_WIFX_L1_4G:
		return (int)model;
	default:
		return -1;
	}
}

const char *model_str(enum wgw_ec_model model)
{
	int index;
	if ((index = model_index(model)) < 0) {
		return unknown_str;
	}
	return model_strs[index];
}

const char *model_pretty_str(enum wgw_ec_model model)
{
	int index;
	if ((index = model_index(model)) < 0) {
		return unknown_str;
	}
	return model_pretty_strs[index];
}

int model_variant_index(enum wgw_ec_model product_model, u8 product_variant)
{
	switch (product_model) {
	case WGW_EC_M_WIFX_L1:
		if (product_variant <= 1) {
			return product_variant;
		}
		break;
	case WGW_EC_M_WIFX_L1_4G:
		if (product_variant <= 2) {
			return product_variant;
		}
		break;
	case WGW_EC_M_LORIX_ONE:
	case WGW_EC_M_WIFX_Y1:
	default:
		break;
	}
	return -1;
}

const char *model_variant_str(enum wgw_ec_model product_model,
			      u8 product_variant)
{
	static const char *model_l1_variant_strs[] = { "8XX", "9XX" };
	static const char *model_l1_4g_variant_strs[] = { "8XX-EU", "9XX-AU",
							  "9XX-US" };

	int index;
	if ((index = model_variant_index(product_model, product_variant)) < 0) {
		return unknown_str;
	}
	switch (product_model) {
	case WGW_EC_M_WIFX_L1:
		return model_l1_variant_strs[product_variant];
	case WGW_EC_M_WIFX_L1_4G:
		return model_l1_4g_variant_strs[product_variant];
	default:
		return unknown_str;
	}
}

static const char *frequency_strs[] = { "863-870", "902-928" };
int frequency_index(enum wgw_ec_frequency frequency)
{
	switch (frequency) {
	case WGW_EC_F_863_870:
	case WGW_EC_F_902_928:
		return (int)frequency;
	default:
		return -1;
	}
}
const char *frequency_str(enum wgw_ec_frequency frequency)
{
	int index;
	if ((index = frequency_index(frequency)) < 0) {
		return unknown_str;
	}
	return frequency_strs[index];
}

int hw_version_str(const struct wgw_ec_version *version, char *version_str,
		   int max_len)
{
	if (version->revision == 0) {
		return snprintf(version_str, max_len, "%d.%d", version->major,
				version->minor);
	} else {
		return snprintf(version_str, max_len, "%d.%d%c", version->major,
				version->minor,
				(char)(version->revision + 'A'));
	}
}

int fw_version_str(const struct wgw_ec_version *version, char *version_str,
		   int max_len)
{
	return snprintf(version_str, max_len, "%d.%d.%d", version->major,
			version->minor, version->revision);
}

#define CMD_LTR_STATUS_SUCCESS 0
#define CMD_LTR_STATUS_BUSY 1
#define CMD_LTR_STATUS_INVALID_ARG 2
#define CMD_LTR_STATUS_FAILURE 3
#define CMD_LTR_STATUS_BAD_CRC 4
#define CMD_LTR_STATUS_NOT_WRITABLE 5
#define CMD_LTR_STATUS_PAGE_NOT_ALIGNED 6

int wgw_ec_get_ltr_status(struct wgw_ec_dev *ec)
{
	struct wgw_ec_device *ec_dev = ec->ec_dev;
	u8 status;
	int ret = ec_dev->read_byte(ec_dev, WGW_EC_REG_CMD_LTR_STATUS, &status);
	if (ret < 0) {
		dev_err(ec->dev,
			"failed to read LTR command status from device\n");
		return ret;
	}
	return (int)status;
}
EXPORT_SYMBOL_GPL(wgw_ec_get_ltr_status);

int wgw_ec_wait_ready(struct wgw_ec_dev *ec)
{
	int i, status;
	for (i = 0; i < 10; i++) {
		status = wgw_ec_get_ltr_status(ec);
		if (status < 0)
			return status;

		if (status == CMD_LTR_STATUS_SUCCESS)
			return 0;

		if (status == CMD_LTR_STATUS_BUSY) {
			msleep(5);
			continue;
		}

		dev_err(ec->dev, "LTR command error status: %d\n", status);
		return -EIO;
	}
	return -EBUSY;
}
EXPORT_SYMBOL_GPL(wgw_ec_wait_ready);

static int mem_slot_get(struct wgw_ec_dev *ec, u8 slot_index,
			struct wgw_ec_memory_slot *slot)
{
	struct wgw_ec_device *ec_dev = ec->ec_dev;
	char buffer[32];
	int ret;

	if (slot_index > 3) {
		dev_err(ec->dev, "slot[%d] doesn't exist\n", slot_index);
		return -EINVAL;
	}

	ret = ec_dev->read_block(ec_dev, WGW_EC_REG_MEM_SLOT0_CTRL + slot_index,
				 buffer);
	if (ret < 0) {
		dev_err(ec->dev,
			"failed to read memory slot[%d] ctrl register\n",
			slot_index);
		return ret;
	}
	slot->flags = buffer[0] & WGW_EC_MEM_SLOT_STATE_Msk;
	slot->length = buffer[1];

	if (!(slot->flags & WGW_EC_MEM_SLOT_SET)) {
		slot->length = 0;
	} else {
		ret = ec_dev->read_block(
			ec_dev, WGW_EC_REG_MEM_SLOT0 + slot_index, buffer);
		if (ret < 0) {
			dev_err(ec->dev,
				"failed to read memory slot[%d] data register\n",
				slot_index);
			return ret;
		}
		if (ret != slot->length) {
			dev_err(ec->dev,
				"failed to read memory slot[%d] data register, data length error\n",
				slot_index);
			return ret;
		}
		memcpy(slot->data, buffer, slot->length);
	}
	return slot->length;
}

int mem_slot_get_str(struct wgw_ec_dev *ec, u8 slot_index,
		     struct wgw_ec_slot_str *slot_str)
{
	int ret;
	struct wgw_ec_memory_slot slot;

	ret = mem_slot_get(ec, slot_index, &slot);
	if (ret < 0) {
		slot_str->state = WGW_EC_MEM_SLOT_STATE_ERROR;
		strcpy(slot_str->data, error_str);
		return ret;
	}
	if (ret > WGW_EC_MEM_SLOT_STR_SIZE) {
		slot_str->state = WGW_EC_MEM_SLOT_STATE_ERROR;
		strcpy(slot_str->data, error_str);
		dev_err(ec->dev,
			"string in slot[%d] read from device is too long (%d)\n",
			slot_index, ret);
		return -EIO;
	}
	slot_str->state = 0;
	if (slot.flags & WGW_EC_MEM_SLOT_OTP) {
		slot_str->state |= WGW_EC_MEM_SLOT_STATE_OTP;
	}
	if (!(slot.flags & WGW_EC_MEM_SLOT_SET)) {
		// slot is not set
		strcpy(slot_str->data, undefined_str);
	} else {
		memcpy(slot_str->data, slot.data, slot.length);
		// convert to null terminated string
		slot_str->data[slot.length] = '\0';
		slot_str->state |= WGW_EC_MEM_SLOT_STATE_SET;
	}
	return 0;
}

int mem_slot_get_u8(struct wgw_ec_dev *ec, u8 slot_index,
		    struct wgw_ec_slot_u8 *slot_u8)
{
	int ret;
	struct wgw_ec_memory_slot slot;

	ret = mem_slot_get(ec, slot_index, &slot);
	if (ret < 0) {
		slot_u8->state = WGW_EC_MEM_SLOT_STATE_ERROR;
		return ret;
	}

	if (ret > 1) {
		dev_err(ec->dev,
			"data in slot[%d] is too big to fit in a u8 (%d)\n",
			slot_index, ret);
		return -EIO;
	}

	// convert standard slot to u8 slot
	slot_u8->state = slot.flags;
	slot_u8->value = slot.data[0];
	return 0;
}

int product_serial_get(struct wgw_ec_dev *ec, struct wgw_ec_slot_str *serial)
{
	int ret = mem_slot_get_str(ec, 0, serial);
	if (ret < 0) {
		dev_err(ec->dev, "error retrieving product serial (%d)\n", ret);
	}
	return ret;
}

int product_model_get(struct wgw_ec_dev *ec, struct wgw_ec_slot_u8 *model)
{
	int ret = mem_slot_get_u8(ec, 1, model);
	if (ret < 0) {
		dev_err(ec->dev, "error retrieving product model (%d)\n", ret);
	}
	return ret;
}

int product_version_get(struct wgw_ec_dev *ec, struct wgw_ec_slot_str *version)
{
	int ret = mem_slot_get_str(ec, 2, version);
	if (ret < 0) {
		dev_err(ec->dev, "error retrieving product version (%d)\n",
			ret);
	}
	return ret;
}

int product_variant_get(struct wgw_ec_dev *ec, struct wgw_ec_slot_u8 *variant)
{
	int ret = mem_slot_get_u8(ec, 3, variant);
	if (ret < 0) {
		dev_err(ec->dev, "error retrieving product variant (%d)\n",
			ret);
	}
	return ret;
}

static int fw_info_get(struct wgw_ec_dev *ec, struct wgw_ec_fw_info *fw_info)
{
	struct wgw_ec_device *ec_dev = ec->ec_dev;
	struct wgw_ec_reg reg;
	int32_t ret;

	// retrieve firmware version
	ret = ec_dev->read_block(ec_dev, WGW_EC_REG_FW_INFO1, reg.data);
	if (ret < 0) {
		dev_err(ec->dev, "failed to read firmware version (%d)\n", ret);
		return ret;
	}
	if (ret != sizeof(reg.fw_info_version)) {
		dev_err(ec->dev,
			"failed to read fw version cause of wrong returned size (%d)\n",
			ret);
		return -EIO;
	}
	fw_info->version = reg.fw_info_version;
	fw_version_str(&reg.fw_info_version, fw_info->version_str,
		       WGW_EC_FW_VERSION_SIZE);

	// retrieve firmware commit hash
	ret = ec_dev->read_block(ec_dev, WGW_EC_REG_FW_INFO2, reg.data);
	if (ret < 0) {
		dev_err(ec->dev, "failed to read firmware commit hash (%d)\n",
			ret);
		return ret;
	}
	if (ret >= WGW_EC_APP_COMMIT_HASH_SIZE) {
		dev_err(ec->dev,
			"firmware commit hash string is too longth (%d)\n",
			ret);
		return -EIO;
	}
	memcpy(&fw_info->commit_hash[0], reg.data, ret);
	// convert to null terminated string
	fw_info->commit_hash[ret] = '\0';

	// retrieve firmware commit date
	ret = ec_dev->read_block(ec_dev, WGW_EC_REG_FW_INFO3, reg.data);
	if (ret < 0) {
		dev_err(ec->dev, "failed to read firmware commit date (%d)\n",
			ret);
		return ret;
	}
	if (ret >= WGW_EC_APP_COMMIT_DATE_SIZE) {
		dev_err(ec->dev,
			"firmware commit date string is too longth (%d)\n",
			ret);
		return -EIO;
	}
	memcpy(&fw_info->commit_date[0], reg.data, ret);
	// convert to null terminated string
	fw_info->commit_date[ret] = '\0';
	return 0;
}

static int mainboard_info_get(struct wgw_ec_dev *ec,
			      struct wgw_ec_mainboard_info *mb_info)
{
	struct wgw_ec_device *ec_dev = ec->ec_dev;
	struct wgw_ec_reg reg;
	//struct wgw_ec_memory_slot slot;
	int32_t ret = 0;

	ret = ec_dev->read_block(ec_dev, WGW_EC_REG_HW_INFO, reg.data);
	if (ret < 0) {
		dev_err(ec->dev, "failed to read hw info (%d)\n", ret);
		return ret;
	}
	if (ret != sizeof(reg.hw_info)) {
		dev_err(ec->dev,
			"failed to read hw info register (wrong returned size)\n");
		return -EIO;
	}
	hw_version_str(&reg.hw_info.version, mb_info->version_str,
		       WGW_EC_HW_VERSION_SIZE);

	// retrieve mainboard model
	mb_info->model.id = mainboard_ref_index(reg.hw_info.model);
	mb_info->model.str = mainboard_ref_str(reg.hw_info.model);

	switch (mb_info->model.id) {
	case WGW_EC_MB_WGW_L02_BASE_L1:
	case WGW_EC_MB_WGW_L02_BASE_L1_4G:
		mb_info->base_model.id = WGW_EC_MB_WGW_L02_BASE_L1;
		mb_info->base_model.str =
			mainboard_ref_str(mb_info->base_model.id);
		break;
	default:
		dev_err(ec->dev, "mainboard model '%s' (%d) is not supported\n",
			mainboard_ref_str(mb_info->model.id),
			mb_info->model.id);
		return -EIO;
	}

	// Legacy stuff, variant is derived from frequency information
	mb_info->variant.id = mainboard_variant_index(reg.hw_info.frequency);
	mb_info->variant.str = mainboard_variant_str(reg.hw_info.frequency);

	// retrieve firmware info
	ret = fw_info_get(ec, &mb_info->fw);
	if (ret < 0) {
		dev_err(ec->dev, "failed to read firmware info (%d)\n", ret);
		return ret;
	}
	return ret;
}

int wgw_ec_boot_state_get(struct wgw_ec_dev *ec, u8 *boot_state)
{
	struct wgw_ec_device *ec_dev = ec->ec_dev;
	int ret = ec_dev->read_byte(ec_dev, WGW_EC_REG_LAST_RESET_STATE,
				    boot_state);
	if (ret < 0) {
		dev_err(ec->dev, "failed to read from device\n");
		*boot_state = 0xFF;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(wgw_ec_boot_state_get);

int wgw_ec_boot_state_clr_update(struct wgw_ec_dev *ec, u8 *boot_state)
{
	struct wgw_ec_device *ec_dev = ec->ec_dev;
	int ret = ec_dev->write_byte(ec_dev, WGW_EC_REG_LAST_RESET_STATE, 0xFF);
	if (ret < 0) {
		dev_err(ec->dev, "failed to write to device\n");
		return ret;
	}
	// return fresh value
	return wgw_ec_boot_state_get(ec, boot_state);
}
EXPORT_SYMBOL_GPL(wgw_ec_boot_state_clr_update);

static int fetch_cache_info(struct wgw_ec_dev *ec)
{
	struct wgw_ec_info *cache = &ec->cache_info;
	struct wgw_ec_device *ec_dev = ec->ec_dev;
	struct device *dev = ec->dev;
	struct wgw_ec_slot_str slot_str;
	struct wgw_ec_slot_u8 slot_u8;
	u8 buffer[32];
	int ret;

	mutex_lock(&ec->cache_lock);

	/* Verify protocole version */
	ret = ec_dev->read_byte(ec_dev, WGW_EC_REG_PROTOC_VER, buffer);
	if (ret < 0) {
		dev_err(dev, "failed to read protocole version (%d)\n", ret);
		goto failure;
	}
	cache->protoc = buffer[0];
	dev_info(dev, "detected wgw-ec, protocol version=%d\n", cache->protoc);
	if (cache->protoc != 2) {
		dev_err(dev, "protocol version %d not supported\n",
			cache->protoc);
		ret = -EPROTO;
		goto failure;
	}

	/* Populate the cache */
	ret = mainboard_info_get(ec, &cache->mainboard);
	if (ret < 0) {
		dev_err(dev, "failed to read mainboard info (%d)\n", ret);
		goto failure;
	}

	// Determine product model
	ret = product_model_get(ec, &slot_u8);
	if (ret < 0 || slot_u8.state < WGW_EC_MEM_SLOT_STATE_SET) {
		if (cache->mainboard.model.id == WGW_EC_MB_WGW_L01_BASE) {
			// the product is a Wifx L1, possibly legacy (no product model written in slot)
			cache->product.model.id = WGW_EC_M_WIFX_L1;
		} else {
			dev_err(dev,
				"could not determine product model, corrupted or undefined\n");
			ret = -EIO;
			goto failure;
		}
	} else {
		cache->product.model.id = slot_u8.value;
	}
	cache->product.model.str = model_str(cache->product.model.id);

	// Determine product variant
	ret = product_variant_get(ec, &slot_u8);
	if (ret < 0 || slot_u8.state < WGW_EC_MEM_SLOT_STATE_SET) {
		if (cache->product.model.id == WGW_EC_M_WIFX_L1) {
			// the product is a Wifx L1, possibly legacy (no product variant written in slot)
			cache->product.variant.id = cache->mainboard.variant.id;
		} else {
			dev_err(dev,
				"could not determine product variant, corrupted or undefined\n");
			ret = -EIO;
			goto failure;
		}
	} else {
		cache->product.variant.id = slot_u8.value;
	}
	ret = model_variant_index(cache->product.model.id,
				  cache->product.variant.id);
	if (ret < 0) {
		dev_err(dev,
			"could not determine or unknown product variant (%d)\n",
			cache->product.variant.id);
		goto failure;
	}
	cache->product.variant.str = model_variant_str(
		cache->product.model.id, cache->product.variant.id);

	// Determine product version
	ret = product_version_get(ec, &slot_str);
	if (ret < 0) {
		if (cache->product.model.id == WGW_EC_M_WIFX_L1) {
			strcpy(cache->product.version_str,
			       cache->mainboard.version_str);
		} else {
			dev_err(dev, "failed to read product version (%d)\n",
				ret);
			goto failure;
		}
	} else {
		strcpy(cache->product.version_str, slot_str.data);
	}

	// Determine product serial
	ret = product_serial_get(ec, &cache->product.serial);
	if (ret < 0) {
		dev_err(dev, "failed to read serial (%d)\n", ret);
		goto failure;
	}

	ret = wgw_ec_boot_state_get(ec, &cache->boot_state);
	if (ret < 0) {
		dev_err(dev, "failed to read boot state (%d)\n", ret);
		goto failure;
	}

	mutex_unlock(&ec->cache_lock);
	return 0;

failure:
	mutex_unlock(&ec->cache_lock);
	return ret;
}

static int display_cache_info(struct wgw_ec_dev *ec)
{
	struct wgw_ec_info *cache = &ec->cache_info;
	struct device *dev = ec->dev;
	int ret = -ENODEV;

	mutex_lock(&ec->cache_lock);

	/* Verify the product is supported */
	switch (cache->product.model.id) {
	case WGW_EC_M_WIFX_L1:
	case WGW_EC_M_WIFX_L1_4G:
		break;
	default:
		dev_err(dev, "Unknown product detected (id=%d)\n",
			cache->mainboard.model.id);
		goto failure;
	}

	if (cache->mainboard.variant.id < 0) {
		dev_err(dev,
			"Mainboard model and/or variant not detected or unknown\n");
		goto failure;
	}

	dev_info(dev, "Found Wifx product, model: %s, variant: %s\n",
		 model_pretty_str((enum wgw_ec_model)cache->product.model.id),
		 cache->product.variant.str);
	if (!cache->product.serial.state) {
		dev_warn(dev, "Serial: %s\n", cache->product.serial.data);
	} else if (cache->product.serial.state < 0) {
		dev_err(dev, " Serial: %s\n", cache->product.serial.data);
	} else {
		dev_info(dev, "Serial: %s\n", cache->product.serial.data);
		if (!(cache->product.serial.state &
		      WGW_EC_MEM_SLOT_STATE_OTP)) {
			dev_warn(dev, "serial is not locked\n");
		} else if (!(cache->product.serial.state &
			     WGW_EC_MEM_SLOT_STATE_SET)) {
			dev_err(dev, "serial is locked with null value\n");
		}
	}
	dev_info(dev, "Product version: %s\n", cache->product.version_str);
	dev_info(dev, "Firmware version: %s (%s) [%s]\n",
		 cache->mainboard.fw.version_str,
		 cache->mainboard.fw.commit_hash,
		 cache->mainboard.fw.commit_date);

	/* Display boot state */
	switch (cache->boot_state) {
	case 0x00:
		dev_info(dev, "Boot: 0x00 (normal mode)\n");
		break;
	case 0x01:
		dev_info(dev, "Boot: 0x01 (factory reset mode)\n");
		break;
	default:
		dev_info(dev,
			 "Boot: 0x%02X (unknown mode), clearing boot state\n",
			 cache->boot_state);
		ret = wgw_ec_boot_state_clr_update(ec, &cache->boot_state);
		if (ret < 0)
			return ret;
		break;
	}

	mutex_unlock(&ec->cache_lock);
	return 0;

failure:
	mutex_unlock(&ec->cache_lock);
	return ret;
}

static void wgw_ec_class_release(struct device *dev)
{
	dev_dbg(dev, "wgw-ec-class release\n");
	kfree(to_wgw_ec_dev(dev));
}

static int wgw_ec_dev_probe(struct platform_device *pdev)
{
	int retval = -ENOMEM;
	struct device *dev = &pdev->dev;
	struct wgw_ec_dev *ec = kzalloc(sizeof(*ec), GFP_KERNEL);

	dev_dbg(dev, "wgw-ec-dev probe\n");

	if (!ec)
		return retval;

	retval = dma_coerce_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (retval)
		return retval;

	mutex_init(&ec->cache_lock);

	dev_set_drvdata(dev, ec);
	ec->ec_dev = dev_get_drvdata(dev->parent);
	ec->dev = dev;
	device_initialize(&ec->class_dev);

	// detect the EC
	retval = fetch_cache_info(ec);
	if (retval) {
		dev_err(dev, "failed to fetch device information\n");
		return retval;
	}
	// diplay info
	display_cache_info(ec);

	/*
	 * Add the class device
	 */
	ec->class_dev.class = &wifx_class;
	ec->class_dev.parent = dev;
	ec->class_dev.release = wgw_ec_class_release;

	retval = dev_set_name(&ec->class_dev, "%s", "wgw-ec");
	if (retval) {
		dev_err(ec->dev, "dev_set_name failed => %d\n", retval);
		goto error_put_device;
	}

	retval = device_add(&ec->class_dev);
	if (retval)
		goto error_put_device;

	// device_register is device_initialize + device_add

	/*
	 * The following subdevices cannot be detected automatically
	 */
	retval = mfd_add_hotplug_devices(ec->dev, wgw_ec_platform_cells,
					 ARRAY_SIZE(wgw_ec_platform_cells));
	if (retval) {
		dev_warn(ec->dev, "failed to add wgw-ec platform devices: %d\n",
			 retval);
		goto error_mfd_remove;
	}

	retval = mfd_add_devices(ec->dev, PLATFORM_DEVID_AUTO, wgw_ec_mfd_cells,
				 ARRAY_SIZE(wgw_ec_mfd_cells), NULL, 0, NULL);
	if (retval) {
		dev_warn(ec->dev, "failed to add wgw-ec subdevice: %d\n",
			 retval);
		goto error_mfd_remove;
	}
	return 0;

error_mfd_remove:
	mfd_remove_devices(ec->dev);
	device_unregister(&ec->class_dev);

error_put_device:
	put_device(&ec->class_dev);
	return retval;
}

static int wgw_ec_dev_remove(struct platform_device *pdev)
{
	struct wgw_ec_dev *ec = dev_get_drvdata(&pdev->dev);
	dev_dbg(ec->dev, "wgw-ec-dev remove\n");

	mfd_remove_devices(ec->dev);
	device_unregister(&ec->class_dev);
	return 0;
}

static const struct platform_device_id wgw_ec_id[] = { { "wgw-ec-dev", 0 },
						       { /* sentinel */ } };
MODULE_DEVICE_TABLE(platform, wgw_ec_id);

static struct platform_driver wgw_ec_dev_driver = {
	.driver = {
		.name = "wgw-ec-dev",
	},
	.id_table = wgw_ec_id,
	.probe = wgw_ec_dev_probe,
	.remove = wgw_ec_dev_remove,
};

static int __init wgw_ec_dev_init(void)
{
	int ret = class_register(&wifx_class);
	if (ret) {
		pr_err("wgw_ec: failed to register device class\n");
		return ret;
	}

	/* Register the driver */
	ret = platform_driver_register(&wgw_ec_dev_driver);
	if (ret < 0) {
		pr_warn("wgw_ec: can't register driver: %d\n", ret);
		goto failed_devreg;
	}
	return 0;

failed_devreg:
	class_unregister(&wifx_class);
	return ret;
}

static void __exit wgw_ec_dev_exit(void)
{
	platform_driver_unregister(&wgw_ec_dev_driver);
	class_unregister(&wifx_class);
}

module_init(wgw_ec_dev_init);
module_exit(wgw_ec_dev_exit);

MODULE_ALIAS("platform:" DRV_NAME);
MODULE_AUTHOR("Yannick Lanz <yannick.lanz@wifx.net>");
MODULE_DESCRIPTION("MFD device driver for the Wifx board Embedded Controller");
MODULE_LICENSE("GPL v2");
