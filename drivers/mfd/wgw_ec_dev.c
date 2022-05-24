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
#include <linux/module.h>
#include <linux/mod_devicetable.h>
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

static const char *model_strs[] = { "lorix-one", "wifx-l1", "wifx-y1" };
static const char *model_pretty_strs[] = { "LORIX One", "Wifx L1", "Wifx Y1" };
int model_index(enum wgw_ec_model model)
{
	switch (model) {
	case WGW_EC_M_LORIX_ONE:
	case WGW_EC_M_WIFX_L1:
	case WGW_EC_M_WIFX_Y1:
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

static const char *variant_strs[] = { "standard" };
int variant_index(enum wgw_ec_variant variant)
{
	switch (variant) {
	case WGW_EC_V_STANDARD:
		return (int)variant;
	default:
		return -1;
	}
}
const char *variant_str(enum wgw_ec_variant variant)
{
	int index;
	if ((index = variant_index(variant)) < 0) {
		return unknown_str;
	}
	return variant_strs[index];
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

static int mem_slot_set(struct wgw_ec_dev *ec, u8 slot_index,
			const struct wgw_ec_memory_slot *slot)
{
	struct wgw_ec_device *ec_dev = ec->ec_dev;
	char buffer[2];
	int ret;

	if (slot_index > 3) {
		dev_err(ec->dev, "slot[%d] doesn't exist\n", slot_index);
		return -EINVAL;
	}

	// XOR compare
	if (!(slot->flags & WGW_EC_MEM_SLOT_SET) != !slot->length) {
		dev_err(ec->dev, "slot[%d] length and flags don't match\n",
			slot_index);
		return -EINVAL;
	}

	buffer[0] = slot->flags;
	buffer[1] = 0; // length has not effect
	mutex_lock(&ec_dev->lock_ltr);
	if (!slot->length) {
		ret = ec_dev->write_block(
			ec_dev, WGW_EC_REG_MEM_SLOT0_CTRL + slot_index, buffer,
			2);
		if (ret < 0) {
			dev_err(ec->dev,
				"failed to write memory slot[%d] ctrl register\n",
				slot_index);
			mutex_unlock(&ec_dev->lock_ltr);
			return ret;
		}
		ret = wgw_ec_wait_ready(ec);
	} else {
		// write data
		ec_dev->write_block(ec_dev, WGW_EC_REG_MEM_SLOT0 + slot_index,
				    slot->data, slot->length);
		if (ret < 0) {
			dev_err(ec->dev,
				"failed to write memory slot[%d] data register\n",
				slot_index);
			mutex_unlock(&ec_dev->lock_ltr);
			return ret;
		}
		if ((ret = wgw_ec_wait_ready(ec)) < 0) {
			mutex_unlock(&ec_dev->lock_ltr);
			return ret;
		}

		// write control register
		ec_dev->write_block(ec_dev,
				    WGW_EC_REG_MEM_SLOT0_CTRL + slot_index,
				    buffer, 2);
		if (ret < 0) {
			dev_err(ec->dev,
				"failed to write memory slot[%d] ctrl register\n",
				slot_index);
			mutex_unlock(&ec_dev->lock_ltr);
			return ret;
		}
		ret = wgw_ec_wait_ready(ec);
	}
	mutex_unlock(&ec_dev->lock_ltr);
	return ret;
}

static int hw_info_get(struct wgw_ec_dev *ec, struct wgw_ec_hw_info *hw_info)
{
	struct wgw_ec_device *ec_dev = ec->ec_dev;
	struct wgw_ec_reg reg;
	int32_t ret;

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
	hw_info->version = reg.hw_info.version;
	hw_version_str(&reg.hw_info.version, hw_info->version_str,
		       WGW_EC_HW_VERSION_SIZE);
	hw_info->model.id = model_index(reg.hw_info.model);
	hw_info->model.str = model_str(reg.hw_info.model);
	hw_info->variant.id = variant_index(reg.hw_info.variant);
	hw_info->variant.str = variant_str(reg.hw_info.variant);
	hw_info->frequency.id = frequency_index(reg.hw_info.frequency);
	hw_info->frequency.str = frequency_str(reg.hw_info.frequency);
	return 0;
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

int serial_get(struct wgw_ec_dev *ec, struct wgw_ec_serial *serial)
{
	int ret;
	struct wgw_ec_memory_slot slot;

	ret = mem_slot_get(ec, 0, &slot);
	if (ret < 0) {
		serial->state = WGW_EC_SERIAL_ERROR;
		strcpy(serial->data, error_str);
		return ret;
	}
	if (ret >= WGW_EC_HW_SN_SIZE) {
		serial->state = WGW_EC_SERIAL_ERROR;
		strcpy(serial->data, error_str);
		dev_err(ec->dev, "serial read from device is too long (%d)\n",
			ret);
		return -EIO;
	}
	serial->state = 0;
	if (slot.flags & WGW_EC_MEM_SLOT_OTP) {
		serial->state |= WGW_EC_SERIAL_OTP;
	}
	if (!(slot.flags & WGW_EC_MEM_SLOT_SET)) {
		// slot is not set
		strcpy(serial->data, undefined_str);
	} else {
		memcpy(serial->data, slot.data, slot.length);
		// convert to null terminated string
		serial->data[slot.length] = '\0';
		serial->state |= WGW_EC_SERIAL_SET;
	}
	return 0;
}

int wgw_ec_serial_set(struct wgw_ec_dev *ec, char *serial,
		      struct wgw_ec_serial *serial_cache)
{
	struct wgw_ec_memory_slot slot;
	size_t len;
	int ret;

	if ((len = strnlen(serial, WGW_EC_HW_SN_SIZE)) == WGW_EC_HW_SN_SIZE) {
		dev_err(ec->dev, "set serial error: serial too long\n");
		return -EINVAL;
	}

	if (!len) {
		dev_err(ec->dev,
			"set serial error: cannot be null (len = 0)\n");
		return -EINVAL;
	}

	slot.flags = WGW_EC_MEM_SLOT_SET_OTP;
	slot.length = len;
	memcpy(slot.data, serial, len);
	ret = mem_slot_set(ec, 0, &slot);
	if (ret < 0) {
		return ret;
	}
	return serial_get(ec, serial_cache);
}
EXPORT_SYMBOL_GPL(wgw_ec_serial_set);

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
	// update local cache
	return wgw_ec_boot_state_get(ec, boot_state);
}
EXPORT_SYMBOL_GPL(wgw_ec_boot_state_clr_update);

static int fetch_cache_info(struct wgw_ec_dev *ec)
{
	struct wgw_ec_info *cache = &ec->cache_info;
	struct wgw_ec_device *ec_dev = ec->ec_dev;
	struct device *dev = ec->dev;
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
	ret = hw_info_get(ec, &cache->hw_info);
	if (ret < 0) {
		dev_err(dev, "failed to read hardware info (%d)\n", ret);
		goto failure;
	}

	ret = fw_info_get(ec, &cache->fw_info);
	if (ret < 0) {
		dev_err(dev, "failed to read firmware info (%d)\n", ret);
		goto failure;
	}

	ret = serial_get(ec, &cache->serial);
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
	if (cache->hw_info.model.id != WGW_EC_M_WIFX_L1) {
		dev_err(dev, "Unknown product detected\n");
		goto failure;
	}

	if (cache->hw_info.model.id < 0 || cache->hw_info.variant.id < 0 ||
	    cache->hw_info.frequency.id < 0) {
		dev_err(dev,
			"Product model, variant and/or frequency not detected or unknown\n");
		goto failure;
	}
	dev_info(dev, "Model: %s (variant=%s, frequency=%s)\n",
		 model_pretty_str((enum wgw_ec_model)cache->hw_info.model.id),
		 cache->hw_info.variant.str, cache->hw_info.frequency.str);
	if (!cache->serial.state) {
		dev_warn(dev, "Serial: %s\n", cache->serial.data);
	} else if (cache->serial.state < 0) {
		dev_err(dev, " Serial: %s\n", cache->serial.data);
	} else {
		dev_info(dev, "Serial: %s\n", cache->serial.data);
		if (!(cache->serial.state & WGW_EC_SERIAL_OTP)) {
			dev_warn(dev, "serial is not locked\n");
		} else if (!(cache->serial.state & WGW_EC_SERIAL_SET)) {
			dev_err(dev, "serial is locked with null value\n");
		}
	}
	dev_info(dev, "HW ver: %s\n", cache->hw_info.version_str);
	dev_info(dev, "FW ver: %s (%s) [%s]\n", cache->fw_info.version_str,
		 cache->fw_info.commit_hash, cache->fw_info.commit_date);

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
