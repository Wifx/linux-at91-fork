/*
 * USB Type-C data mode trigger for the Wifx board EC LED
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
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#include <linux/mfd/wgw-ec/core.h>
#include <linux/mfd/wgw-ec/usbc.h>

struct wgw_usbc_trig_data {
	spinlock_t lock;

	struct led_classdev *led_cdev;
	struct device *usbc_dev;
	struct wgw_ec_usbc_dev *usbc;

	struct delayed_work work;
	struct notifier_block notifier;

	u8 led_on;
	u8 enabled;
};

static ssize_t enable_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct wgw_usbc_trig_data *trigger_data = led_trigger_get_drvdata(dev);
	ssize_t len;

	spin_lock_bh(&trigger_data->lock);
	len = sprintf(buf, "%u\n", trigger_data->enabled);
	spin_unlock_bh(&trigger_data->lock);

	return len;
}

static ssize_t enable_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t size)
{
	struct wgw_usbc_trig_data *trigger_data = led_trigger_get_drvdata(dev);
	int enable;
	char newline;

	switch (sscanf(buf, "%d%c", &enable, &newline)) {
	case 2:
		if (newline != '\n')
			return -EINVAL;

		if (enable > 1)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	cancel_delayed_work_sync(&trigger_data->work);

	spin_lock_bh(&trigger_data->lock);
	trigger_data->enabled = enable;
	schedule_delayed_work(&trigger_data->work, 0);
	spin_unlock_bh(&trigger_data->lock);

	return size;
}

static DEVICE_ATTR_RW(enable);
static struct attribute *wgw_usbc_trig_attrs[] = { &dev_attr_enable.attr,
						   NULL };
ATTRIBUTE_GROUPS(wgw_usbc_trig);

static int wgw_usbc_trig_notify(struct notifier_block *notifier,
				unsigned long evt, void *dv)
{
	struct wgw_usbc_trig_data *trigger_data =
		container_of(notifier, struct wgw_usbc_trig_data, notifier);

	pr_debug("wgw-usbc-trig notify notified by wgw-ec-usbc\n");

	if (evt == WGW_USBC_DATA_MODE_CHANGE) {
		enum usb_data_mode data_mode =
			wgw_ec_usbc_get_data_mode(trigger_data->usbc);

		cancel_delayed_work_sync(&trigger_data->work);

		pr_debug(
			"trigger notified for data mode change, new value=%d\n",
			data_mode);

		spin_lock_bh(&trigger_data->lock);
		trigger_data->led_on =
			(data_mode == USB_DATA_MODE_DEVICE ? 1 : 0);
		schedule_delayed_work(&trigger_data->work, 0);
		spin_unlock_bh(&trigger_data->lock);

		return NOTIFY_OK;
	}
	return NOTIFY_DONE;
}

static void wgw_usbc_trig_work(struct work_struct *work)
{
	struct wgw_usbc_trig_data *trigger_data =
		container_of(work, struct wgw_usbc_trig_data, work.work);

	pr_debug("trigger update to new value=%d\n",
		 trigger_data->led_on && trigger_data->enabled);

	// update of the LED here
	if (trigger_data->led_on && trigger_data->enabled) {
		led_set_brightness(trigger_data->led_cdev, LED_FULL);
	} else {
		led_set_brightness(trigger_data->led_cdev, LED_OFF);
	}
}

static int wgw_usbc_trig_activate(struct led_classdev *led_cdev)
{
	// retrieve the attached LED
	struct device *dev = led_cdev->dev;
	struct platform_device *pdev_trigger;
	struct device_node *of_node;
	struct of_phandle_args of_trigger_handle;
	struct wgw_usbc_trig_data *trigger_data;
	enum usb_data_mode data_mode;
	int count, err, ret;

	dev_dbg(dev, "wgw-usbc-trig activate\n");

	if (!dev) {
		pr_err("wgw-usbc-data-mode: no LED device attached\n");
		return -ENODEV;
	}
	dev_dbg(dev, "registering trigger for led %s\n", led_cdev->name);

	/* Retrieve the of node */
	if (!dev->fwnode) {
		dev_err(dev, "wgw-usbc-data-mode: no LED dev fwnode\n");
		return -ENODEV;
	}
	of_node = to_of_node(dev->fwnode);

	/* Find trigger sources for this LED */
	count = of_count_phandle_with_args(of_node, "trigger-sources",
					   "#trigger-source-cells");
	if (count == -ENOENT) {
		dev_err(dev, "wgw-usbc-data-mode: no trigger phandle found\n");
		goto put_of_node;
	} else if (count < 0) {
		dev_err(dev,
			"wgw-usbc-data-mode: Failed to get trigger sources for %pOF\n",
			of_node);
		goto put_of_node;
	} else if (count != 1) {
		dev_err(dev,
			"wgw-usbc-data-mode: Too much trigger sources (%d), max is 1\n",
			count);
		goto put_of_node;
	}

	/* Retrieve the trigger source phandle */
	err = of_parse_phandle_with_args(of_node, "trigger-sources",
					 "#trigger-source-cells", 0,
					 &of_trigger_handle);
	if (err) {
		dev_err(dev,
			"wgw-usbc-data-mode: Failed to get trigger source phandle: %d\n",
			err);
		goto put_of_node;
	}
	/* Only supported trigger source is wgw-ec-usbc */
	if (!of_device_is_compatible(of_trigger_handle.np,
				     "wifx,wgw-ec-usbc")) {
		dev_err(dev,
			"wgw-usbc-data-mode: %s is not a compatible trigger source\n",
			of_trigger_handle.np->name);
		goto put_trigger_handle;
	}
	pdev_trigger = of_find_device_by_node(of_trigger_handle.np);
	if (IS_ERR_OR_NULL(pdev_trigger)) {
		dev_err(dev,
			"wgw-usbc-data-mode: platform device from of_device not found\n");
		goto put_trigger_handle;
	}
	of_node_put(of_trigger_handle.np);
	of_node_put(of_node);

	trigger_data = kzalloc(sizeof(struct wgw_usbc_trig_data), GFP_KERNEL);
	if (!trigger_data) {
		goto put_device;
	}

	/* we know this plaftorm device contains a wgw_ec_usbc_dev */
	trigger_data->usbc_dev = &pdev_trigger->dev;
	trigger_data->usbc = (struct wgw_ec_usbc_dev *)dev_get_drvdata(
		trigger_data->usbc_dev);

	spin_lock_init(&trigger_data->lock);
	trigger_data->notifier.notifier_call = wgw_usbc_trig_notify;
	trigger_data->notifier.priority = 10;
	INIT_DELAYED_WORK(&trigger_data->work, wgw_usbc_trig_work);

	trigger_data->led_cdev = led_cdev;
	led_set_trigger_data(led_cdev, trigger_data);

	ret = blocking_notifier_chain_register(
		&trigger_data->usbc->notifier_list, &trigger_data->notifier);
	if (ret) {
		kfree(trigger_data);
		goto put_device;
	}

	/* Init LED state */
	data_mode = wgw_ec_usbc_get_data_mode(trigger_data->usbc);
	spin_lock_bh(&trigger_data->lock);
	trigger_data->led_on = (data_mode == USB_DATA_MODE_DEVICE ? 1 : 0);
	schedule_delayed_work(&trigger_data->work, 0);
	spin_unlock_bh(&trigger_data->lock);

	return ret;

put_device:
	put_device(&pdev_trigger->dev);
put_trigger_handle:
	of_node_put(of_trigger_handle.np);
put_of_node:
	of_node_put(of_node);
	return -ENODEV;
}

static void wgw_usbc_trig_deactivate(struct led_classdev *led_cdev)
{
	struct wgw_usbc_trig_data *trigger_data =
		led_get_trigger_data(led_cdev);

	pr_debug("wgw-usbc-trig deactivate\n");

	blocking_notifier_chain_unregister(&trigger_data->usbc->notifier_list,
					   &trigger_data->notifier);

	cancel_delayed_work_sync(&trigger_data->work);

	if (trigger_data->usbc_dev)
		put_device(trigger_data->usbc_dev);

	kfree(trigger_data);
}

static struct led_trigger wgw_usbc_led_trigger = {
	.name = "wgw-usbc-data-mode",
	.activate = wgw_usbc_trig_activate,
	.deactivate = wgw_usbc_trig_deactivate,
	.groups = wgw_usbc_trig_groups,
};

static int __init wgw_usbc_trig_init(void)
{
	pr_debug("wgw-usbc-trig init\n");
	return led_trigger_register(&wgw_usbc_led_trigger);
}

static void __exit wgw_usbc_trig_exit(void)
{
	pr_debug("wgw-usbc-trig exit\n");
	led_trigger_unregister(&wgw_usbc_led_trigger);
}

module_init(wgw_usbc_trig_init);
module_exit(wgw_usbc_trig_exit);

MODULE_AUTHOR("Yannick Lanz <yannick.lanz@wifx.net>");
MODULE_DESCRIPTION("USB Type-C data mode trigger for the Wifx board EC LED");
MODULE_LICENSE("GPL v2");