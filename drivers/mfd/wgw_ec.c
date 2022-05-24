/*
 * MFD driver core for the Wifx board Embedded Controller
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
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>

#include <linux/mfd/core.h>
#include <linux/mfd/wgw-ec/core.h>
#include <linux/mfd/wgw-ec/reg.h>

#define DRV_NAME "wgw-ec"

static irqreturn_t wgw_ec_irq_thread(int irq, void *data)
{
	struct wgw_ec_device *ec_dev = data;
	dev_dbg(ec_dev->dev, "wgw-ec irq thread run\n");

	blocking_notifier_call_chain(&ec_dev->notifier_list, 0, ec_dev);
	return IRQ_HANDLED;
}

int wgw_ec_register(struct wgw_ec_device *ec_dev)
{
	struct device *dev = ec_dev->dev;
	int err = 0;

	BLOCKING_INIT_NOTIFIER_HEAD(&ec_dev->notifier_list);
	mutex_init(&ec_dev->lock_ltr);

	if (ec_dev->irq) {
		err = devm_request_threaded_irq(
			dev, ec_dev->irq, NULL, wgw_ec_irq_thread,
			IRQF_TRIGGER_RISING | IRQF_ONESHOT, "wgw-ec-irq",
			ec_dev);
		if (err) {
			dev_err(dev, "Failed to request IRQ %d: %d\n",
				ec_dev->irq, err);
			return err;
		}
		/* Clear the interrupt flag */
		ec_dev->write_byte(ec_dev, WGW_EC_REG_INTERRUPT, 0xFF);
	}

	ec_dev->cpu_state_pin =
		devm_gpiod_get_optional(dev, "cpu-state", GPIOF_DIR_OUT);
	if (ec_dev->cpu_state_pin) {
		if (IS_ERR(ec_dev->cpu_state_pin)) {
			dev_err(dev, "Failed to request GPIO cpu-state: %ld\n",
				PTR_ERR(ec_dev->cpu_state_pin));
			return PTR_ERR(ec_dev->cpu_state_pin);
		}
		gpiod_set_value(ec_dev->cpu_state_pin, true);
	} else {
		dev_warn(
			dev,
			"No cpu-state gpio provided, functionnalities will be limited\n");
	}

	/* Register a platform device for the main EC instance */
	ec_dev->ec = platform_device_register_data(
		ec_dev->dev, "wgw-ec-dev", PLATFORM_DEVID_AUTO, NULL, 0);
	if (IS_ERR(ec_dev->ec)) {
		dev_err(ec_dev->dev,
			"Failed to create Wifx OS EC platform device\n");
		return PTR_ERR(ec_dev->ec);
	}

	dev_info(dev, "Wifx board EC device registered\n");

	return 0;
}
EXPORT_SYMBOL_GPL(wgw_ec_register);

int wgw_ec_unregister(struct wgw_ec_device *ec_dev)
{
	dev_dbg(ec_dev->dev, "wgw-ec unregister\n");
	platform_device_unregister(ec_dev->ec);
	return 0;
}
EXPORT_SYMBOL_GPL(wgw_ec_unregister);

MODULE_AUTHOR("Yannick Lanz <yannick.lanz@wifx.net>");
MODULE_DESCRIPTION("MFD driver core for the Wifx board Embedded Controller");
MODULE_LICENSE("GPL v2");
