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
	/* trigger object platform device */
	struct platform_device *trig_pdev;

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
	struct wgw_ec_usbc_notification *notif =
		(struct wgw_ec_usbc_notification *)dv;
	struct wgw_usbc_trig_data *trigger_data =
		container_of(notifier, struct wgw_usbc_trig_data, notifier);

	struct device *dev = trigger_data->led_cdev->dev;

	dev_dbg(dev, "wgw-usbc-trig notify notified by wgw-ec-usbc\n");

	if (notif->dev != &trigger_data->trig_pdev->dev)
	{
		dev_dbg(dev, "LED trigger not concerned by the wgw-ec-usbc event\n");
		return NOTIFY_DONE;
	}

	if (evt == WGW_USBC_DATA_MODE_CHANGE ||
	    evt == WGW_USBC_DEVICE_PROBE ||
	    evt == WGW_USBC_NOTIFIER_UPDATE) {
		cancel_delayed_work_sync(&trigger_data->work);

		pr_debug(
			"trigger notified for data mode change, new value=%d\n",
			notif->data_mode);

		spin_lock_bh(&trigger_data->lock);
		trigger_data->led_on =
			(notif->data_mode == USB_DATA_MODE_DEVICE ? 1 : 0);
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
	struct device_node *of_node;
	struct of_phandle_args of_trigger_handle;
	struct wgw_usbc_trig_data *trigger_data;
	int ret = -ENODEV, count;

	dev_dbg(dev, "wgw-usbc-trig activate\n");

	if (!dev) {
		pr_err("wgw-usbc-data-mode: no LED device attached\n");
		return ret;
	}
	dev_dbg(dev, "registering trigger for led %s\n", led_cdev->name);

	trigger_data = kzalloc(sizeof(struct wgw_usbc_trig_data), GFP_KERNEL);
	if (!trigger_data)
		return -ENOMEM;

	/* Retrieve the of node */
	if (!dev->fwnode) {
		dev_err(dev, "wgw-usbc-data-mode: no LED dev fwnode\n");
		return ret;
	}
	of_node = to_of_node(dev->fwnode);

	/* Find trigger sources for this LED */
	count = of_count_phandle_with_args(of_node, "trigger-sources",
					   "#trigger-source-cells");
	if (count == -ENOENT) {
		dev_err(dev, "wgw-usbc-data-mode: no trigger phandle found\n");
		return ret;
	} else if (count < 0) {
		dev_err(dev,
			"wgw-usbc-data-mode: Failed to get trigger sources for %pOF\n",
			of_node);
		return ret;
	} else if (count != 1) {
		dev_err(dev,
			"wgw-usbc-data-mode: Too much trigger sources (%d), max is 1\n",
			count);
		return ret;
	}

	/* Retrieve the trigger source phandle */
	ret = of_parse_phandle_with_args(of_node, "trigger-sources",
					 "#trigger-source-cells", 0,
					 &of_trigger_handle);
	if (ret) {
		dev_err(dev,
			"wgw-usbc-data-mode: Failed to get trigger source phandle: %d\n",
			ret);
		return ret;
	}
	/* Only supported trigger source is wgw-ec-usbc */
	if (!of_device_is_compatible(of_trigger_handle.np,
				     "wifx,wgw-ec-usbc")) {
		dev_err(dev,
			"wgw-usbc-data-mode: %s is not a compatible trigger source\n",
			of_trigger_handle.np->name);
		goto put_trigger_handle;
	}
	trigger_data->trig_pdev = of_find_device_by_node(of_trigger_handle.np);
	if (IS_ERR_OR_NULL(trigger_data->trig_pdev)) {
		dev_err(dev,
			"wgw-usbc-data-mode: platform device from of_device not found\n");
		goto put_trigger_handle;
	}

	trigger_data->led_cdev = led_cdev;
	trigger_data->led_on = 0;
	trigger_data->enabled = 0;

	spin_lock_init(&trigger_data->lock);
	INIT_DELAYED_WORK(&trigger_data->work, wgw_usbc_trig_work);

	led_set_trigger_data(led_cdev, trigger_data);

	trigger_data->notifier.notifier_call = wgw_usbc_trig_notify;
	trigger_data->notifier.priority = 10;
	// prepare notifier
	ret = wgw_ec_usbc_register_notifier(&trigger_data->notifier);
	if (ret < 0) {
		dev_err(dev, "error registering usbc notifier: %d\n", ret);
		goto put_device;
	}

	of_node_put(of_trigger_handle.np);
	return 0;

put_device:
	put_device(&trigger_data->trig_pdev->dev);
put_trigger_handle:
	of_node_put(of_trigger_handle.np);
	return ret;
}

static void wgw_usbc_trig_deactivate(struct led_classdev *led_cdev)
{
	struct wgw_usbc_trig_data *trigger_data =
		led_get_trigger_data(led_cdev);

	pr_debug("wgw-usbc-trig deactivate\n");

	wgw_ec_usbc_unregister_notifier(&trigger_data->notifier);

	cancel_delayed_work_sync(&trigger_data->work);

	if (trigger_data->trig_pdev)
		put_device(&trigger_data->trig_pdev->dev);

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