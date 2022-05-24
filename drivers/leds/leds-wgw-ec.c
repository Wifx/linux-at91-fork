/*
 * LED driver for the Wifx board EC MFD driver
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
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/workqueue.h>

#include <linux/mfd/wgw-ec/core.h>
#include <linux/mfd/wgw-ec/reg.h>

#define DRV_NAME "wgw-ec-leds"

static const struct of_device_id led_wgw_ec_of_match[] = {
	{
		.compatible = "wifx,wgw-ec-leds",
	},
	{},
};
MODULE_DEVICE_TABLE(of, led_wgw_ec_of_match);

struct led_wgw_ec {
	const char *name;
	const char *default_trigger;
	s32 reg;
	u8 active_low;
	unsigned max_brightness;
	struct device_node *of_node;
};

struct led_wgw_ec_data {
	struct led_classdev cdev;
	struct wgw_ec_dev *ec;
	u8 id;
	unsigned int active_low;
	struct device_node *of_node;
	/* delayed work*/
	struct work_struct work;
};

struct led_wgw_ec_priv {
	int num_leds;
	struct wgw_ec_dev *ec;
	struct led_wgw_ec_data leds[0];
};

// private prototypes
static int led_wgw_ec_remove(struct platform_device *pdev);

static void led_wgw_ec_set_work(struct work_struct *work)
{
	struct led_wgw_ec_data *led_data =
		container_of(work, struct led_wgw_ec_data, work);
	struct wgw_ec_device *ec_dev = led_data->ec->ec_dev;

	u16 brightness = led_data->cdev.brightness;
	if (led_data->active_low) {
		brightness = led_data->cdev.max_brightness - brightness;
	}
	ec_dev->write_word(ec_dev, WGW_EC_REG_LED_START + led_data->id,
			   brightness);
}

static void led_wgw_ec_set(struct led_classdev *led_cdev,
			   enum led_brightness value)
{
	struct led_wgw_ec_data *led_data =
		container_of(led_cdev, struct led_wgw_ec_data, cdev);

	led_cdev->brightness = value;
	schedule_work(&led_data->work);
}

static int led_wgw_ec_add(struct device *dev, struct led_wgw_ec_priv *priv,
			  struct led_wgw_ec *led)
{
	struct led_wgw_ec_data *led_data = &priv->leds[priv->num_leds];
	struct led_init_data init_data = {};
	int ret;

	led_data->ec = priv->ec;
	if (led->reg < 0) {
		led_data->id = (u8)priv->num_leds;
	} else {
		led_data->id = (u8)led->reg;
	}
	led_data->active_low = led->active_low;
	// keep track of dt node
	led_data->of_node = led->of_node;
	led_data->cdev.name = led->name;
	led_data->cdev.default_trigger = led->default_trigger;
	led_data->cdev.max_brightness = led->max_brightness;
	led_data->cdev.brightness = LED_OFF;
	if (led->active_low) {
		led_data->cdev.brightness = led->max_brightness;
	}
	led_data->cdev.brightness_set = led_wgw_ec_set;

	INIT_WORK(&led_data->work, led_wgw_ec_set_work);

	init_data.fwnode = of_fwnode_handle(led->of_node);
	ret = led_classdev_register_ext(dev, &led_data->cdev, &init_data);
	if (!ret) {
		priv->num_leds++;
	} else {
		dev_err(dev, "failed to register wgw-ec led for %s: %d\n",
			led->name, ret);
	}
	dev_info(dev, "registered led (name=%s, trigger=%s)\n",
		 led_data->cdev.name, led_data->cdev.default_trigger);
	return ret;
}

static int led_wgw_ec_probe_dt(struct device *dev, struct led_wgw_ec_priv *priv)
{
	struct device_node *np, *child;
	struct led_wgw_ec led;
	int ret = 0;

	memset(&led, 0, sizeof(led));

	np = of_node_get(dev->of_node);
	// increment refcount of each child
	for_each_child_of_node (np, child) {
		ret = of_property_read_string(child, "label", &led.name);
		if (ret) {
			dev_err(dev, "field label not found in dt\n");
			goto out_node_put;
		}

		ret = of_property_read_u32(child, "reg", &led.reg);
		if (ret) {
			dev_err(dev, "field reg not found in dt\n");
			goto out_node_put;
		}

		ret = of_property_read_string(child, "linux,default-trigger",
					      &led.default_trigger);
		if (ret) {
			dev_err(dev, "field default-trigger not found in dt\n");
			goto out_node_put;
		}

		ret = of_property_read_u32(child, "max-brightness",
					   &led.max_brightness);
		if (ret) {
			led.max_brightness = LED_FULL;
		}
		led.active_low = of_property_read_bool(child, "active-low");

		led.of_node = child;
		ret = led_wgw_ec_add(dev, priv, &led);
		if (ret)
			goto out_node_put;
	}

out_node_put:
	of_node_put(np);
	if (ret)
		of_node_put(child);
	return ret;
}

static int led_wgw_ec_probe(struct platform_device *pdev)
{
	struct wgw_ec_dev *ec = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct device_node *parent = ec->ec_dev->dev->of_node;
	struct led_wgw_ec_priv *priv;
	int count;
	int ret = -EINVAL;

	if (!ec) {
		dev_err(dev, "no parent EC device\n");
		return ret;
	}

	if (!parent) {
		dev_err(dev, "no parent of node\n");
		return ret;
	}

	dev->of_node = of_get_compatible_child(parent,
					       led_wgw_ec_of_match->compatible);
	if (!dev->of_node) {
		dev_err(dev, "no compatible led node in dt\n");
		goto put_node;
	}

	count = of_get_child_count(dev->of_node);
	dev_dbg(dev, "detect %d led node(s)\n", count);

	if (!count) {
		dev_err(dev, "no led node found\n");
		goto put_node;
	}

	priv = devm_kzalloc(dev, struct_size(priv, leds, count), GFP_KERNEL);
	if (!priv) {
		ret = -ENOMEM;
		goto put_node;
	}
	priv->ec = ec;
	platform_set_drvdata(pdev, priv);

	ret = led_wgw_ec_probe_dt(dev, priv);
	if (ret) {
		dev_dbg(dev, "create from fwnode failed, cleanup\n");
		led_wgw_ec_remove(pdev);
		goto put_node;
	}

	return 0;

put_node:
	of_node_put(dev->of_node);
	return ret;
}

static int led_wgw_ec_remove(struct platform_device *pdev)
{
	struct led_wgw_ec_priv *priv = platform_get_drvdata(pdev);
	struct wgw_ec_device *ec_dev = priv->ec->ec_dev;
	struct led_wgw_ec_data *led_data;
	int i;
	u16 brightness, max_brightness;

	for (i = 0; i < priv->num_leds; i++) {
		led_data = &priv->leds[i];
		brightness = 0;
		max_brightness = led_data->cdev.max_brightness;

		led_classdev_unregister(&led_data->cdev);
		cancel_work_sync(&led_data->work);
		of_node_put(led_data->of_node);

		if (led_data->active_low) {
			brightness = led_data->cdev.max_brightness - brightness;
		}
		ec_dev->write_word(ec_dev, WGW_EC_REG_LED_START + led_data->id,
				   brightness);
	}
	of_node_put(pdev->dev.of_node);
	return 0;
}

static struct platform_driver led_wgw_ec_driver = {
	.driver = {
		.name	= "wgw-ec-leds",
		.of_match_table = of_match_ptr(led_wgw_ec_of_match),
	},
	.probe = led_wgw_ec_probe,
	.remove   = led_wgw_ec_remove,
};
module_platform_driver(led_wgw_ec_driver);

MODULE_ALIAS("platform:" DRV_NAME);
MODULE_AUTHOR("Yannick Lanz <yannick.lanz@wifx.net>");
MODULE_DESCRIPTION("LED support for the Wifx board EC");
MODULE_LICENSE("GPL v2");
