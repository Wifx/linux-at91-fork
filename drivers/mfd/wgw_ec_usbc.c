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
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/mfd/wgw-ec/core.h>
#include <linux/mfd/wgw-ec/reg.h>
#include <linux/mfd/wgw-ec/usbc.h>

#define DRV_NAME "wgw-ec-usbc"

static const struct of_device_id wgw_ec_usbc_of_match[] = {
	{
		.compatible = "wifx,wgw-ec-usbc",
	},
	{},
};
MODULE_DEVICE_TABLE(of, wgw_ec_usbc_of_match);

static const char *power_mode_to_str(enum usb_power_mode mode)
{
	static const char *power_mode_str[] = { "detached", "source", "sink",
						"error" };
	if (mode < USB_POWER_MODE_ERROR) {
		return power_mode_str[mode];
	}
	return power_mode_str[USB_POWER_MODE_ERROR];
}

static const char *data_mode_to_str(enum usb_data_mode mode)
{
	static const char *data_mode_str[] = { "device", "host", "error" };
	if (mode < USB_DATA_MODE_ERROR) {
		return data_mode_str[mode];
	}
	return data_mode_str[USB_DATA_MODE_ERROR];
}

static int power_mode_get(struct wgw_ec_usbc_dev *usbc,
			  enum usb_power_mode *usb_power_mode)
{
	u8 reg;
	struct wgw_ec_device *ec_dev = usbc->ec->ec_dev;

	int ret = ec_dev->read_byte(ec_dev, WGW_EC_REG_USB_MODE_POWER, &reg);
	if (ret < 0)
		return ret;

	*usb_power_mode = reg;

	switch (*usb_power_mode) {
	case USB_POWER_MODE_DETACHED:
	case USB_POWER_MODE_SOURCE:
	case USB_POWER_MODE_SINK:
		return ret;
	default:
		return -EIO;
	}
}

static int data_mode_get(struct wgw_ec_usbc_dev *usbc,
			 enum usb_data_mode *usb_data_mode)
{
	u8 reg;
	struct wgw_ec_device *ec_dev = usbc->ec->ec_dev;

	int ret = ec_dev->read_byte(ec_dev, WGW_EC_REG_USB_MODE_DATA, &reg);
	if (ret < 0)
		return -EIO;

	*usb_data_mode = reg;

	switch (*usb_data_mode) {
	case USB_DATA_MODE_DEVICE:
	case USB_DATA_MODE_HOST:
		return ret;
	default:
		return -EIO;
	}
}

static int data_mode_set(struct wgw_ec_usbc_dev *usbc,
			 enum usb_data_mode usb_data_mode)
{
	u8 reg = usb_data_mode;
	struct wgw_ec_device *ec_dev = usbc->ec->ec_dev;

	switch (reg) {
	case USB_DATA_MODE_DEVICE:
	case USB_DATA_MODE_HOST:
		return ec_dev->write_byte(ec_dev, WGW_EC_REG_USB_MODE_DATA,
					  reg);
	default:
		return -EINVAL;
	}
}

static int wgw_ec_usbc_connect(struct wgw_ec_usbc_dev *usbc,
			       enum usb_power_mode usb_power_mode)
{
	struct typec_partner_desc desc;

	if (usbc->partner || usb_power_mode == USB_POWER_MODE_DETACHED)
		return 0;

	desc.usb_pd = false;
	desc.accessory = TYPEC_ACCESSORY_NONE;
	desc.identity = NULL;

	usbc->power_mode = usb_power_mode;
	typec_set_pwr_role(usbc->port, usb_power_mode);

	usbc->partner = typec_register_partner(usbc->port, &desc);
	if (IS_ERR(usbc->partner))
		return PTR_ERR(usbc->partner);

	return 0;
}

static void wgw_ec_usbc_disconnect(struct wgw_ec_usbc_dev *usbc,
				   enum usb_power_mode usb_power_mode)
{
	if (usb_power_mode != USB_POWER_MODE_DETACHED)
		return;

	if (!IS_ERR(usbc->partner))
		typec_unregister_partner(usbc->partner);
	usbc->partner = NULL;

	usbc->power_mode = usb_power_mode;
	typec_set_pwr_role(usbc->port, TYPEC_SINK);
}

static int wgw_ec_usbc_dr_set(struct typec_port *port,
			      enum typec_data_role role)
{
	struct wgw_ec_usbc_dev *usbc = typec_get_drvdata(port);
	ssize_t status;

	pr_debug("wgw_ec_usbc_dr_set: %d\n", role);
	status = data_mode_set(usbc, (enum usb_data_mode)role);
	if (status < 0) {
		return status;
	}

	typec_set_data_role(usbc->port, role);
	return 0;
}

static int wgw_ec_usbc_trig_notify(struct notifier_block *nb, unsigned long evt,
				   void *dv)
{
	struct wgw_ec_usbc_dev *usbc =
		container_of(nb, struct wgw_ec_usbc_dev, notifier);
	struct wgw_ec_device *ec_dev = usbc->ec->ec_dev;
	enum usb_data_mode usb_data_mode;
	enum usb_power_mode usb_power_mode;
	int ret;
	u8 reg;

	pr_debug("wgw-ec-usbc notified by wgw-ec-core\n");

	ret = ec_dev->read_byte(ec_dev, WGW_EC_REG_INTERRUPT, &reg);
	if (ret < 0) {
		dev_err(usbc->dev, "failed to read register ISR register\n");
		return NOTIFY_DONE;
	}

	if (reg & WGW_USBC_DATA_MODE_CHANGE) {
		ret = data_mode_get(usbc, &usb_data_mode);
		if (ret < 0) {
			dev_err(usbc->dev,
				"failed to read USB-C data mode register\n");
			goto failure;
		}
		spin_lock_bh(&usbc->lock);
		if (usbc->data_mode != usb_data_mode) {
			usbc->data_mode = usb_data_mode;
			spin_unlock_bh(&usbc->lock);

			typec_set_data_role(usbc->port, usb_data_mode);
			blocking_notifier_call_chain(&usbc->notifier_list,
						     WGW_USBC_DATA_MODE_CHANGE,
						     NULL);
		} else {
			spin_unlock_bh(&usbc->lock);
		}
	}

	if (reg & WGW_USBC_POWER_MODE_CHANGE) {
		ret = power_mode_get(usbc, &usb_power_mode);
		if (ret < 0) {
			dev_err(usbc->dev,
				"failed to read USB-C power mode register\n");
			goto failure;
		}
		spin_lock_bh(&usbc->lock);
		if (usbc->power_mode != usb_power_mode) {
			spin_unlock_bh(&usbc->lock);
			if (usbc->power_mode == USB_POWER_MODE_DETACHED) {
				pr_debug("USB-C attach event\n");

				ret = wgw_ec_usbc_connect(usbc, usb_power_mode);
				if (ret)
					dev_err(usbc->dev,
						"failed to register partner\n");
			}
			if (usb_power_mode == USB_POWER_MODE_DETACHED) {
				pr_debug("USB-C detach event\n");
				wgw_ec_usbc_disconnect(usbc, usb_power_mode);
			}

			blocking_notifier_call_chain(&usbc->notifier_list,
						     WGW_USBC_POWER_MODE_CHANGE,
						     NULL);
		} else {
			spin_unlock_bh(&usbc->lock);
		}
	}

	/* Clear the interrupt flag */
	ret = ec_dev->write_byte(ec_dev, WGW_EC_REG_INTERRUPT, reg);
	if (ret < 0)
		dev_err(usbc->dev, "failed to clear register ISR register\n");
	return NOTIFY_OK;

failure:
	/* Clear the interrupt flag */
	ret = ec_dev->write_byte(ec_dev, WGW_EC_REG_INTERRUPT,
				 (WGW_USBC_DATA_MODE_CHANGE |
				  WGW_USBC_POWER_MODE_CHANGE));
	if (ret < 0)
		dev_err(usbc->dev, "failed to read register ISR register\n");
	return NOTIFY_OK;
}

enum usb_data_mode wgw_ec_usbc_get_data_mode(struct wgw_ec_usbc_dev *usbc)
{
	enum usb_data_mode data_mode;
	spin_lock_bh(&usbc->lock);
	data_mode = usbc->data_mode;
	spin_unlock_bh(&usbc->lock);
	return data_mode;
}
EXPORT_SYMBOL(wgw_ec_usbc_get_data_mode);

enum usb_power_mode wgw_ec_usbc_get_power_mode(struct wgw_ec_usbc_dev *usbc)
{
	enum usb_power_mode power_mode;
	spin_lock_bh(&usbc->lock);
	power_mode = usbc->power_mode;
	spin_unlock_bh(&usbc->lock);
	return power_mode;
}
EXPORT_SYMBOL(wgw_ec_usbc_get_power_mode);

static const struct typec_operations wgw_ec_usbc_ops = {
	.dr_set = wgw_ec_usbc_dr_set
};

static int wgw_ec_usbc_probe(struct platform_device *pdev)
{
	struct wgw_ec_dev *ec = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct device_node *of_parent = ec->ec_dev->dev->of_node;
	struct wgw_ec_usbc_dev *usbc;
	int ret;

	usbc = devm_kzalloc(dev, sizeof(struct wgw_ec_usbc_dev), GFP_KERNEL);
	if (!usbc)
		return -ENOMEM;

	usbc->ec = ec;
	usbc->dev = dev;
	platform_set_drvdata(pdev, usbc);

	dev->of_node = of_get_compatible_child(
		of_parent, wgw_ec_usbc_of_match->compatible);
	if (!dev->of_node) {
		dev_err(dev, "no compatible usb node in dt\n");
		goto put_node;
	}

	/* Retrieve USB-C power and data mode */
	power_mode_get(usbc, &usbc->power_mode);
	data_mode_get(usbc, &usbc->data_mode);

	dev_info(dev,
		 "USB-C controller detected: power mode=%s, data mode=%s\n",
		 power_mode_to_str(usbc->power_mode),
		 data_mode_to_str(usbc->data_mode));

	spin_lock_init(&usbc->lock);
	BLOCKING_INIT_NOTIFIER_HEAD(&usbc->notifier_list);
	usbc->notifier.priority = 10;
	usbc->notifier.notifier_call = wgw_ec_usbc_trig_notify;
	ret = blocking_notifier_chain_register(&ec->ec_dev->notifier_list,
					       &usbc->notifier);
	if (ret < 0) {
		dev_err(dev, "failed to register to wgw-ec notifier list\n");
		goto put_node;
	}

	usbc->typec_cap.revision = USB_TYPEC_REV_1_1;
	usbc->typec_cap.prefer_role = TYPEC_SINK;
	usbc->typec_cap.type = TYPEC_PORT_DRP;
	usbc->typec_cap.data = TYPEC_PORT_DRD;
	usbc->typec_cap.ops = &wgw_ec_usbc_ops;
	usbc->typec_cap.driver_data = usbc;
	usbc->port = typec_register_port(dev, &usbc->typec_cap);
	if (IS_ERR(usbc->port)) {
		ret = PTR_ERR(usbc->port);
		goto put_node;
	}

	typec_set_data_role(usbc->port, usbc->data_mode);
	wgw_ec_usbc_connect(usbc, usbc->power_mode);
	return 0;

put_node:
	of_node_put(dev->of_node);
	return ret;
}

static int wgw_ec_usbc_remove(struct platform_device *pdev)
{
	struct wgw_ec_usbc_dev *usbc = platform_get_drvdata(pdev);
	pr_debug("wgw-ec-usbc remove\n");
	blocking_notifier_chain_unregister(&usbc->ec->ec_dev->notifier_list,
					   &usbc->notifier);
	wgw_ec_usbc_disconnect(usbc, USB_POWER_MODE_DETACHED);
	typec_unregister_port(usbc->port);
	of_node_put(pdev->dev.of_node);
	return 0;
}

static struct platform_driver wgw_ec_usbc_driver = {
	.driver = {
		.name = "wgw-ec-usbc",
		.of_match_table = of_match_ptr(wgw_ec_usbc_of_match),
	},
	.probe = wgw_ec_usbc_probe,
	.remove = wgw_ec_usbc_remove,
};
module_platform_driver(wgw_ec_usbc_driver);

MODULE_ALIAS("platform:" DRV_NAME);
MODULE_AUTHOR("Yannick Lanz <yannick.lanz@wifx.net>");
MODULE_DESCRIPTION("USB Type-C support for the Wifx board EC");
MODULE_LICENSE("GPL v2");
