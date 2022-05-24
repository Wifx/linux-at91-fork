/*
 * I2C driver for the Wifx board EC
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
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>

#include <linux/mfd/wgw-ec/core.h>
#include <linux/mfd/wgw-ec/reg.h>

// Returns length read (>= 0) or error (< 0)
static int read_byte(struct wgw_ec_device *wgw_dev, char command, u8 *data)
{
	struct i2c_client *client = wgw_dev->priv;
	s32 result = i2c_smbus_read_byte_data(client, command);
	if (result < 0) {
		return result;
	}
	*data = (u8)result;
	return 1;
}

static int read_word(struct wgw_ec_device *wgw_dev, char command, u16 *data)
{
	struct i2c_client *client = wgw_dev->priv;
	s32 result = i2c_smbus_read_word_data(client, command);
	if (result < 0) {
		return result;
	}
	*data = (u16)result;
	return 1;
}

static int read_block(struct wgw_ec_device *wgw_dev, char command, u8 *data)
{
	struct i2c_client *client = wgw_dev->priv;
	return (int)i2c_smbus_read_block_data(client, command, data);
}

// Returns length written (>= 0) or error (< 0)
static int write_byte(struct wgw_ec_device *wgw_dev, char command, u8 data)
{
	struct i2c_client *client = wgw_dev->priv;
	s32 result = i2c_smbus_write_byte_data(client, command, data);
	if (result < 0) {
		return result;
	}
	return 1;
}

static int write_word(struct wgw_ec_device *wgw_dev, char command, u16 data)
{
	struct i2c_client *client = wgw_dev->priv;
	s32 result = i2c_smbus_write_word_data(client, command, data);
	if (result < 0) {
		return result;
	}
	return 2;
}

static int write_block(struct wgw_ec_device *wgw_dev, char command,
		       const u8 *data, u8 len)
{
	struct i2c_client *client = wgw_dev->priv;
	return (int)i2c_smbus_write_block_data(client, command, len, data);
}

static int wgw_ec_i2c_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct wgw_ec_device *wgw_dev = NULL;
	int err;

	pr_debug("wgw-ec-i2c probe\n");

	wgw_dev = devm_kzalloc(&client->dev, sizeof(*wgw_dev), GFP_KERNEL);
	if (wgw_dev == NULL)
		return -ENOMEM;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&client->dev, "SMBUS Byte Data not Supported\n");
		return -EIO;
	}
	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_WORD_DATA)) {
		dev_err(&client->dev, "SMBUS Byte Data not Supported\n");
		return -EIO;
	}
	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_BLOCK_DATA)) {
		dev_err(&client->dev, "SMBUS Byte Data not Supported\n");
		return -EIO;
	}
	if (i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_PEC)) {
		client->flags |= I2C_CLIENT_PEC;
	} else {
		dev_err(&client->dev,
			"SMBUS PEC is not supported, pass in unchecked mode\n");
	}

	i2c_set_clientdata(client, wgw_dev);
	wgw_dev->dev = dev;
	wgw_dev->priv = client;
	wgw_dev->irq = client->irq;
	wgw_dev->read_byte = read_byte;
	wgw_dev->read_word = read_word;
	wgw_dev->read_block = read_block;
	wgw_dev->write_byte = write_byte;
	wgw_dev->write_word = write_word;
	wgw_dev->write_block = write_block;
	wgw_dev->phys_name = client->adapter->name;

	err = wgw_ec_register(wgw_dev);
	if (err) {
		dev_err(dev, "cannot register EC\n");
		return err;
	}

	return 0;
}

static int wgw_ec_i2c_remove(struct i2c_client *client)
{
	struct wgw_ec_device *wgw_dev = i2c_get_clientdata(client);
	pr_debug("wgw-ec-i2c remove\n");
	return wgw_ec_unregister(wgw_dev);
}

static const struct i2c_device_id wgw_ec_i2c_id[] = { { "wgw-ec-i2c", 0 }, {} };
MODULE_DEVICE_TABLE(client, wgw_ec_i2c_id);

static const struct of_device_id wgw_ec_of_match[] = {
	{
		.compatible = "wifx,wgw-ec-i2c",
	},
	{}
};
MODULE_DEVICE_TABLE(of, wgw_ec_of_match);

static struct i2c_driver wgw_ec_i2c_driver = {
	.driver = {
		.name = "wgw-ec-i2c",
		.of_match_table = of_match_ptr(wgw_ec_of_match),
	},
	.probe = wgw_ec_i2c_probe,
	.remove = wgw_ec_i2c_remove,
	.id_table = wgw_ec_i2c_id,
};

module_i2c_driver(wgw_ec_i2c_driver);

MODULE_AUTHOR("Yannick Lanz <yannick.lanz@wifx.net>");
MODULE_DESCRIPTION("I2C driver for the Wifx board EC");
MODULE_LICENSE("GPL v2");
