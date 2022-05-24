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
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/i2c.h>

#include <linux/mfd/wgw-ec/core.h>
#include <linux/mfd/wgw-ec/chardev.h>

#define DRV_NAME "wgw-ec-chardev"

struct chardev_data {
	struct wgw_ec_dev *ec;
	struct miscdevice misc;
};

struct chardev_priv {
	struct wgw_ec_dev *ec;
};

static noinline int wgw_ec_ioctl_pkt_cmd(struct wgw_ec_dev *ec, u8 read_write,
					 u8 command, u32 size,
					 union i2c_smbus_data __user *data)
{
	union i2c_smbus_data temp = {};
	int datasize, res;
	struct wgw_ec_device *ec_dev = ec->ec_dev;

	dev_dbg(ec->dev, "wgw_ec_ioctl_pkt_cmd\n");
	dev_dbg(ec->dev, "read_write: %d\n", read_write);
	dev_dbg(ec->dev, "   command: %d\n", command);
	dev_dbg(ec->dev, "      size: %d\n", size);

	if ((size != I2C_SMBUS_BYTE_DATA) && (size != I2C_SMBUS_WORD_DATA) &&
	    (size != I2C_SMBUS_BLOCK_DATA)) {
		dev_dbg(ec->dev,
			"size out of range (%x) in ioctl WGW_EC_DEV_IOC_PKT_CMD.\n",
			size);
		return -EINVAL;
	}

	if ((read_write != I2C_SMBUS_READ) && (read_write != I2C_SMBUS_WRITE)) {
		dev_dbg(ec->dev,
			"read_write out of range (%x) in ioctl WGW_EC_DEV_IOC_PKT_CMD.\n",
			read_write);
		return -EINVAL;
	}

	if (size == I2C_SMBUS_BYTE_DATA)
		datasize = sizeof(data->byte);
	else if (size == I2C_SMBUS_WORD_DATA)
		datasize = sizeof(data->word);
	else
		datasize = sizeof(data->block);

	if (read_write == I2C_SMBUS_WRITE) {
		if (copy_from_user(&temp, data, datasize))
			return -EFAULT;
	}

	switch (size) {
	case I2C_SMBUS_BYTE_DATA:
		if (read_write == I2C_SMBUS_READ) {
			res = ec_dev->read_byte(ec_dev, command, &temp.byte);
		} else {
			res = ec_dev->write_byte(ec_dev, command, temp.byte);
		}
		break;

	case I2C_SMBUS_WORD_DATA:
		if (read_write == I2C_SMBUS_READ) {
			res = ec_dev->read_word(ec_dev, command, &temp.word);
		} else {
			res = ec_dev->write_word(ec_dev, command, temp.word);
		}
		break;

	case I2C_SMBUS_BLOCK_DATA: {
		if (read_write == I2C_SMBUS_READ) {
			res = ec_dev->read_block(ec_dev, command,
						 &temp.block[1]);
			if (res > 0) {
				temp.block[0] = res;
			}
		} else {
			u8 length = temp.block[0];
			if (length > I2C_SMBUS_BLOCK_MAX)
				length = I2C_SMBUS_BLOCK_MAX;
			res = ec_dev->write_block(ec_dev, command,
						  &temp.block[1], length);
		}
		break;
	}

	default:
		dev_dbg(ec->dev, "fallback\n");
		return 0;
	}

	if (res < 0) {
		dev_err(ec->dev, "failed to run command: %d\n", res);
		return res;
	}

	if (read_write == I2C_SMBUS_READ) {
		if (copy_to_user(data, &temp, datasize))
			return -EFAULT;
	}
	return 0;
}

static noinline int wgw_ec_ioctl_pkt_cmd_ltr(struct wgw_ec_dev *ec,
					     u8 read_write, u8 command,
					     u32 size,
					     union i2c_smbus_data __user *data)
{
	struct wgw_ec_device *ec_dev = ec->ec_dev;
	int res;

	// send command
	mutex_lock(&ec_dev->lock_ltr);
	res = wgw_ec_ioctl_pkt_cmd(ec, read_write, command, size, data);
	if (res < 0) {
		mutex_unlock(&ec_dev->lock_ltr);
		return res;
	}

	// wait ready
	res = wgw_ec_wait_ready(ec);
	mutex_unlock(&ec_dev->lock_ltr);
	return res;
}

/*
 * Device file ops
 */
static long wgw_ec_chardev_ioctl(struct file *filp, unsigned int cmd,
				 unsigned long arg)
{
	struct chardev_priv *priv = filp->private_data;
	struct wgw_ec_dev *ec = priv->ec;

	/*dev_info(ec->dev, "wgw_ioctl\n  cmd: 0x%08X\n  arg: 0x%08lX\n", cmd,
		 arg);*/

	if (_IOC_TYPE(cmd) != WGW_EC_DEV_IOC)
		return -ENOTTY;

	switch (cmd) {
	case WGW_EC_DEV_IOC_PKT_CMD: {
		struct i2c_smbus_ioctl_data data_arg;

		if (copy_from_user(&data_arg,
				   (struct i2c_smbus_ioctl_data __user *)arg,
				   sizeof(struct i2c_smbus_ioctl_data)))
			return -EFAULT;

		return wgw_ec_ioctl_pkt_cmd(ec, data_arg.read_write,
					    data_arg.command, data_arg.size,
					    data_arg.data);
	}

	case WGW_EC_DEV_IOC_PKT_CMD_LTR: {
		struct i2c_smbus_ioctl_data data_arg;

		if (copy_from_user(&data_arg,
				   (struct i2c_smbus_ioctl_data __user *)arg,
				   sizeof(struct i2c_smbus_ioctl_data)))
			return -EFAULT;

		return wgw_ec_ioctl_pkt_cmd_ltr(ec, data_arg.read_write,
						data_arg.command, data_arg.size,
						data_arg.data);
	}
	}
	return 0;
}

static int wgw_ec_chardev_open(struct inode *inode, struct file *filp)
{
	struct miscdevice *mdev = filp->private_data;
	struct wgw_ec_dev *ec = dev_get_drvdata(mdev->parent);
	struct chardev_priv *priv;

	pr_debug("wgw-ec-chardev open\n");

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->ec = ec;
	filp->private_data = priv;

	return 0;
}

static int wgw_ec_chardev_release(struct inode *inode, struct file *filp)
{
	struct chardev_priv *priv = filp->private_data;
	//struct wgw_ec_device *ec = priv->ec_dev;
	kfree(priv);
	return 0;
}

static const struct file_operations chardev_fops = {
	.open = wgw_ec_chardev_open,
	.unlocked_ioctl = wgw_ec_chardev_ioctl,
	.release = wgw_ec_chardev_release,
};

static int wgw_ec_chardev_probe(struct platform_device *pdev)
{
	struct wgw_ec_dev *ec = dev_get_drvdata(pdev->dev.parent);
	struct chardev_data *data;
	int ret;

	dev_dbg(&pdev->dev, "wgw-ec-chardev probe\n");

	/* Create a char device: we want to create it anew */
	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->ec = ec;
	data->misc.minor = MISC_DYNAMIC_MINOR;
	data->misc.fops = &chardev_fops;
	data->misc.name = "wgw-ec";
	data->misc.parent = pdev->dev.parent;

	dev_set_drvdata(&pdev->dev, data);

	ret = misc_register(&data->misc);
	if (ret < 0) {
		dev_warn(&pdev->dev,
			 "failed to register chardev misc device. err=%d\n",
			 ret);
		return ret;
	}
	dev_info(&pdev->dev, "registered chardev misc device\n");
	return 0;
}

static int wgw_ec_chardev_remove(struct platform_device *pdev)
{
	struct chardev_data *data = dev_get_drvdata(&pdev->dev);

	dev_dbg(&pdev->dev, "wgw-ec-chardev remove\n");

	misc_deregister(&data->misc);

	return 0;
}

static struct platform_driver wgw_ec_chardev_driver = {
	.driver = {
		.name = DRV_NAME,
	},
	.probe = wgw_ec_chardev_probe,
	.remove = wgw_ec_chardev_remove,
};

module_platform_driver(wgw_ec_chardev_driver);

MODULE_ALIAS("platform:" DRV_NAME);
MODULE_AUTHOR("Yannick Lanz <yannick.lanz@wifx.net>");
MODULE_DESCRIPTION("Miscellaneous character driver for Wifx board EC");
MODULE_LICENSE("GPL v2");