/*
 * Driver to communicate with the LORIX One PMIC/Reset controller
 *
 *  Copyright (C) 2016 Wifx,
 *                2016 Yannick Lanz <yannick.lanz@wifx.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#ifndef __LINUX_PMIC_LORIX_ONE_H
#define __LINUX_PMIC_LORIX_ONE_H
#include <linux/leds.h>

struct attiny {
    struct device *dev;
    struct i2c_client *client;

    struct mutex lock;

    unsigned long id;

    // attiny cached values
    void *cache;
    // subdevice
    struct device *machine_dev;
};

/*
 * LEDs subdevice platform data
 */
struct attiny_led_platform_data {
    int id;
    const char *name;
    const char *default_trigger;
};

#define MAX_LED_CONTROL_REGS    1

struct attiny_leds_platform_data {
    struct attiny_led_platform_data *led;
    int num_leds;
    u32 led_control[MAX_LED_CONTROL_REGS];
};

/*
 * GPIOs subdevice bits and masks
 */
struct lorix_gpio_platform_data {
    unsigned gpio_start;
    u8 gpio_en_mask;
    u8 gpio_pullup_mask;
};

/*
 * MFD chip platform data
 */
struct attiny_platform_data {
    unsigned int flags;

    struct attiny_leds_platform_data *leds;
};

/*
 * MFD chip functions
 */
extern int pmic_lorix_read(struct attiny *attiny, int reg, uint8_t *val);
extern int pmic_lorix_write(struct attiny *attiny, int reg, uint8_t val);

#endif /* __LINUX_PMIC_LORIX_ONE_H */
