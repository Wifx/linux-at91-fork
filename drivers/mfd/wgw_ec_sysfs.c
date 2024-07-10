/*
 * Expose the Wifx board EC through sysfs
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
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#include <linux/mfd/wgw-ec/core.h>

#define DRV_NAME "wgw-ec-sysfs"

static ssize_t dev_version_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", "3.0.0");
}

static ssize_t product_model_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct wgw_ec_dev *ec = to_wgw_ec_dev(dev);
	struct wgw_ec_info *cache = &ec->cache_info;
	ssize_t len;

	mutex_lock(&ec->cache_lock);
	len = sprintf(buf, "%s\n", cache->product.model.str);
	mutex_unlock(&ec->cache_lock);
	return len;
}

static ssize_t product_model_id_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct wgw_ec_dev *ec = to_wgw_ec_dev(dev);
	struct wgw_ec_info *cache = &ec->cache_info;
	ssize_t len;

	mutex_lock(&ec->cache_lock);
	if (cache->product.model.id < 0) {
		len = sprintf(buf, "%s\n", cache->product.model.str);
	} else {
		len = sprintf(buf, "%d\n", cache->product.model.id);
	}
	mutex_unlock(&ec->cache_lock);

	return len;
}

static ssize_t product_variant_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct wgw_ec_dev *ec = to_wgw_ec_dev(dev);
	struct wgw_ec_info *cache = &ec->cache_info;
	ssize_t len;

	mutex_lock(&ec->cache_lock);
	len = sprintf(buf, "%s\n", cache->product.variant.str);
	mutex_unlock(&ec->cache_lock);
	return len;
}

static ssize_t product_variant_id_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct wgw_ec_dev *ec = to_wgw_ec_dev(dev);
	struct wgw_ec_info *cache = &ec->cache_info;
	ssize_t len;

	mutex_lock(&ec->cache_lock);
	if (cache->product.variant.id < 0) {
		len = sprintf(buf, "%s\n", cache->product.variant.str);
	} else {
		len = sprintf(buf, "%d\n", cache->product.variant.id);
	}
	mutex_unlock(&ec->cache_lock);

	return len;
}

static ssize_t product_version_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct wgw_ec_dev *ec = to_wgw_ec_dev(dev);
	struct wgw_ec_info *cache = &ec->cache_info;
	ssize_t len;

	mutex_lock(&ec->cache_lock);
	len = sprintf(buf, "%s\n", cache->product.version_str);
	mutex_unlock(&ec->cache_lock);
	return len;
}

static ssize_t product_serial_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct wgw_ec_dev *ec = to_wgw_ec_dev(dev);
	struct wgw_ec_info *cache = &ec->cache_info;
	ssize_t len;

	mutex_lock(&ec->cache_lock);
	len = sprintf(buf, "%s\n", cache->product.serial.data);
	mutex_unlock(&ec->cache_lock);
	return len;
}

static ssize_t boot_state_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct wgw_ec_dev *ec = to_wgw_ec_dev(dev);
	struct wgw_ec_info *cache = &ec->cache_info;
	u8 boot_state;

	mutex_lock(&ec->cache_lock);
	boot_state = cache->boot_state;
	mutex_unlock(&ec->cache_lock);

	return sprintf(buf, "%d\n", boot_state);
}

static ssize_t boot_state_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct wgw_ec_dev *ec = to_wgw_ec_dev(dev);
	struct wgw_ec_info *cache = &ec->cache_info;
	ssize_t ret;
	unsigned int inval;

	ret = count - 1;
	if (!ret || ret > 1 || sscanf(buf, "%du\n", &inval) <= 0 || inval > 1)
		return -EINVAL;

	mutex_lock(&ec->cache_lock);
	ret = wgw_ec_boot_state_clr_update(ec, &cache->boot_state);
	mutex_unlock(&ec->cache_lock);

	if (ret < 0) {
		return -EIO;
	}
	return count;
}

static ssize_t mem_ram_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	return sprintf(buf, "%d\n", 256 * 1024);
}

static ssize_t mem_nand_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	return sprintf(buf, "%d\n", 1024 * 1024);
}

static ssize_t fw_version_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct wgw_ec_dev *ec = to_wgw_ec_dev(dev);
	struct wgw_ec_info *cache = &ec->cache_info;
	ssize_t len;

	mutex_lock(&ec->cache_lock);
	len = sprintf(buf, "%s\n", cache->mainboard.fw.version_str);
	mutex_unlock(&ec->cache_lock);
	return len;
}

static ssize_t fw_version_hash_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct wgw_ec_dev *ec = to_wgw_ec_dev(dev);
	struct wgw_ec_info *cache = &ec->cache_info;
	ssize_t len;

	mutex_lock(&ec->cache_lock);
	len = sprintf(buf, "%s\n", cache->mainboard.fw.commit_hash);
	mutex_unlock(&ec->cache_lock);
	return len;
}

static ssize_t fw_version_date_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct wgw_ec_dev *ec = to_wgw_ec_dev(dev);
	struct wgw_ec_info *cache = &ec->cache_info;
	ssize_t len;

	mutex_lock(&ec->cache_lock);
	len = sprintf(buf, "%s\n", cache->mainboard.fw.commit_date);
	mutex_unlock(&ec->cache_lock);
	return len;
}

static DEVICE_ATTR_RO(dev_version);
static DEVICE_ATTR_RO(product_model);
static DEVICE_ATTR_RO(product_model_id);
static DEVICE_ATTR_RO(product_variant);
static DEVICE_ATTR_RO(product_variant_id);
static DEVICE_ATTR_RO(product_version);
static DEVICE_ATTR_RO(product_serial);
static DEVICE_ATTR(boot_state,
		   (S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP | S_IROTH),
		   boot_state_show, boot_state_store);
static DEVICE_ATTR_RO(mem_ram);
static DEVICE_ATTR_RO(mem_nand);
static DEVICE_ATTR_RO(fw_version);
static DEVICE_ATTR_RO(fw_version_hash);
static DEVICE_ATTR_RO(fw_version_date);

static struct attribute *wgw_attrs[] = {
	&dev_attr_dev_version.attr,
	&dev_attr_product_model.attr,
	&dev_attr_product_model_id.attr,
	&dev_attr_product_variant.attr,
	&dev_attr_product_variant_id.attr,
	&dev_attr_product_version.attr,
	&dev_attr_product_serial.attr,
	&dev_attr_boot_state.attr,
	&dev_attr_mem_ram.attr,
	&dev_attr_mem_nand.attr,
	&dev_attr_fw_version.attr,
	&dev_attr_fw_version_hash.attr,
	&dev_attr_fw_version_date.attr,
	/* sentinel */
	NULL,
};

static const struct attribute_group wgw_ec_attr_group = {
	.attrs = wgw_attrs,
};

static int wgw_ec_sysfs_probe(struct platform_device *pdev)
{
	struct wgw_ec_dev *ec = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	int ret;

	pr_debug("wgw-ec-sysfs probe\n");

	platform_set_drvdata(pdev, ec);

	ret = sysfs_create_group(&ec->class_dev.kobj, &wgw_ec_attr_group);
	if (ret < 0) {
		dev_err(dev, "failed to create sysfs attributes. err=%d\n",
			ret);
		return ret;
	}
	dev_info(&pdev->dev, "registered sysfs attributes\n");
	return 0;
}

static int wgw_ec_sysfs_remove(struct platform_device *pdev)
{
	struct wgw_ec_dev *ec = platform_get_drvdata(pdev);

	pr_debug("wgw-ec-sysfs remove\n");

	sysfs_remove_group(&ec->class_dev.kobj, &wgw_ec_attr_group);
	return 0;
}

static struct platform_driver wgw_ec_sysfs_driver = {
	.driver = {
		.name = DRV_NAME,
	},
	.probe = wgw_ec_sysfs_probe,
	.remove = wgw_ec_sysfs_remove,
};

module_platform_driver(wgw_ec_sysfs_driver);

MODULE_ALIAS("platform:" DRV_NAME);
MODULE_AUTHOR("Yannick Lanz <yannick.lanz@wifx.net>");
MODULE_DESCRIPTION("Expose the Wifx board EC through sysfs");
MODULE_LICENSE("GPL v2");
