/*
 * Driver to communicate with the LORIX One PMIC/Reset controller
 *
 *  Copyright (C) 2016-2020 Wifx,
 *                2016-2020 Yannick Lanz <yannick.lanz@wifx.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/of.h>

#include <linux/leds.h>

#include <linux/mfd/pmic-lorix.h>
#include <linux/mfd/core.h>

#define REG_LAST_RESET_STATE        0x00
#define REG_LED_BRIGTHNESS          0x01
#define REG_FW_VERSION_LENGTH       0x02
#define REG_FW_VERSION              0x03
#define REG_HW_VERSION_LENGTH       0x04
#define REG_HW_VERSION              0x05
#define REG_PRODUCT_NAME_LENGTH     0x06
#define REG_PRODUCT_NAME            0x07
#define REG_PRODUCT_TYPE_LENGTH     0x08
#define REG_PRODUCT_TYPE            0x09
#define REG_FEATURE1                0x0A
#define REG_FEATURE2                0x0B

static struct mfd_cell pmic_lorix_devs[] = {
    {
        .name = "pmic-lorix-led",
        .of_compatible = "wifx,pmic-lorix-led",
    },
};

typedef struct __attribute__((packed, aligned(1))){
    union{
        uint8_t raw;
        struct{
            uint8_t     FEAT_LEGACY : 1;
            uint8_t     FEAT_BOOT   : 1;
            uint8_t     FEAT_FW_VER : 1;
            uint8_t     FEAT_HW_VER : 1;
            uint8_t     FEAT_NAME   : 1;
            uint8_t     FEAT_TYPE   : 1;
            uint8_t     FEAT_PROG   : 1;
            uint8_t     FEAT_UNUSED : 1;
        } bit;
    };
} reg_feature1_t;

typedef struct __attribute__((packed, aligned(1))){
    union{
        uint8_t raw;
        struct{
            uint8_t     FEAT_UNUSED : 8;
        } bit;
    };
} reg_feature2_t;

typedef struct {
    reg_feature1_t      feature1;
    reg_feature2_t      feature2;
    char                fw_ver[16];
    char                hw_ver[16];
    char                name[16];
    char                type[16];
} attiny_cache_t;

static int __lorix_read(struct i2c_client *client,
                int reg, uint8_t *val)
{
    int ret;

    ret = i2c_smbus_read_byte_data(client, reg);
    if (ret < 0) {
        dev_err(&client->dev, "failed reading at 0x%02x\n", reg);
        return ret;
    }

    *val = (uint8_t)ret;
    return 0;
}

static int __lorix_write(struct i2c_client *client,
                 int reg, uint8_t val)
{
    int ret;

    ret = i2c_smbus_write_byte_data(client, reg, val);
    if (ret < 0) {
        dev_err(&client->dev, "failed writing 0x%02x to 0x%02x\n",
                val, reg);
        return ret;
    }
    return 0;
}

int pmic_lorix_write(struct attiny *attiny, int reg, uint8_t val)
{
    int ret;
    mutex_lock(&attiny->lock);

    ret = __lorix_write(attiny->client, reg, val);

    mutex_unlock(&attiny->lock);
    return ret;
}
EXPORT_SYMBOL(pmic_lorix_write);

int pmic_lorix_read(struct attiny *attiny, int reg, uint8_t *val)
{
    int ret;
    mutex_lock(&attiny->lock);

    ret = __lorix_read(attiny->client, reg, val);

    mutex_unlock(&attiny->lock);
    return ret;
}
EXPORT_SYMBOL(pmic_lorix_read);

static int boot_state_get(struct attiny *attiny, uint8_t *boot_state)
{
    int ret = pmic_lorix_read(attiny, REG_LAST_RESET_STATE, boot_state);
    if (ret < 0)
        *boot_state = 0xFF;

    return ret;
}

static int boot_state_clr(struct attiny *attiny)
{
    return pmic_lorix_write(attiny, REG_LAST_RESET_STATE, 0xFF);
}

static int fw_version_get(struct attiny *attiny, char *str)
{
    int ret;
    uint8_t len;
    char tmp[10];

    mutex_lock(&attiny->lock);

    ret = i2c_smbus_read_byte_data(attiny->client, REG_FW_VERSION_LENGTH);
    if (ret < 0) {
        dev_err(attiny->dev, "failed reading register FW_VERSION_LENGTH\n");
        goto out_err_read;
    }
    len = (uint8_t)ret;
    if (len > 10) {
        dev_err(attiny->dev, "error with FW version length (length read = %d)\n", len);
        goto out_err_read;
    }

    ret = i2c_smbus_read_i2c_block_data(attiny->client, REG_FW_VERSION, (uint8_t)ret, tmp);
    if (ret < 0) {
        dev_err(attiny->dev, "failed reading register FW_VERSION\n");
        goto out_err_read;
    }

    mutex_unlock(&attiny->lock);

    strncpy(str, tmp, len);
    return (int)len;

out_err_read:
    mutex_unlock(&attiny->lock);
    dev_err(attiny->dev, "failed retrieving FW version\n");
    return -EIO;
}

static int hw_version_get(struct attiny *attiny, char *str)
{
    int ret;
    uint8_t len;
    char tmp[10];

    mutex_lock(&attiny->lock);

    ret = i2c_smbus_read_byte_data(attiny->client, REG_HW_VERSION_LENGTH);
    if (ret < 0) {
        dev_err(attiny->dev, "failed reading register HW_VERSION_LENGTH\n");
        goto out_err_read;
    }
    len = (uint8_t)ret;
    if (len > 10) {
        dev_err(attiny->dev, "error with HW version length (length read = %d)\n", len);
        goto out_err_read;
    }

    ret = i2c_smbus_read_i2c_block_data(attiny->client, REG_HW_VERSION, (uint8_t)ret, tmp);
    if (ret < 0) {
        dev_err(attiny->dev, "failed reading register HW_VERSION\n");
        goto out_err_read;
    }

    mutex_unlock(&attiny->lock);

    strncpy(str, tmp, len);
    return (int)len;

out_err_read:
    mutex_unlock(&attiny->lock);
    dev_err(attiny->dev, "failed retrieving HW version\n");
    return -EIO;
}

static int product_name_get(struct attiny *attiny, char *str)
{
    int ret;
    uint8_t len;
    char tmp[16];

    mutex_lock(&attiny->lock);

    ret = i2c_smbus_read_byte_data(attiny->client, REG_PRODUCT_NAME_LENGTH);
    if (ret < 0) {
        dev_err(attiny->dev, "failed reading register PRODUCT_NAME_LENGTH\n");
        goto out_err_read;
    }
    len = (uint8_t)ret;
    if (len > 16) {
        dev_err(attiny->dev, "error with product name length (length read = %d)\n", len);
        goto out_err_read;
    }

    ret = i2c_smbus_read_i2c_block_data(attiny->client, REG_PRODUCT_NAME, (uint8_t)ret, tmp);
    if (ret < 0) {
        dev_err(attiny->dev, "failed reading register PRODUCT_NAME\n");
        goto out_err_read;
    }

    mutex_unlock(&attiny->lock);

    strncpy(str, tmp, len);
    return (int)len;

out_err_read:
    mutex_unlock(&attiny->lock);
    dev_err(attiny->dev, "failed retrieving product name\n");
    return -EIO;
}

static int product_type_get(struct attiny *attiny, char *str)
{
    int ret;
    uint8_t len;
    char tmp[16];

    mutex_lock(&attiny->lock);

    ret = i2c_smbus_read_byte_data(attiny->client, REG_PRODUCT_TYPE_LENGTH);
    if (ret < 0) {
        dev_err(attiny->dev, "failed reading register PRODUCT_TYPE_LENGTH\n");
        goto out_err_read;
    }
    len = (uint8_t)ret;
    if (len > 16) {
        dev_err(attiny->dev, "error with product type length (length read = %d)\n", len);
        goto out_err_read;
    }

    ret = i2c_smbus_read_i2c_block_data(attiny->client, REG_PRODUCT_TYPE, (uint8_t)ret, tmp);
    if (ret < 0) {
        dev_err(attiny->dev, "failed reading register PRODUCT_TYPE\n");
        goto out_err_read;
    }

    mutex_unlock(&attiny->lock);

    strncpy(str, tmp, len);
    return (int)len;

out_err_read:
    mutex_unlock(&attiny->lock);
    dev_err(attiny->dev, "failed retrieving product type\n");
    return -EIO;
}

static int reg_feature1_get(struct attiny *attiny, reg_feature1_t *reg_feature1)
{
    int ret = pmic_lorix_read(attiny, REG_FEATURE1, &reg_feature1->raw);
    if (ret < 0)
        reg_feature1->raw = 0x00;

    return ret;
}

static int reg_feature2_get(struct attiny *attiny, reg_feature2_t *reg_feature2)
{
    int ret = pmic_lorix_read(attiny, REG_FEATURE2, &reg_feature2->raw);
    if (ret < 0)
        reg_feature2->raw = 0x00;

    return ret;
}

static ssize_t dev_version_show(struct device *dev,
                struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%s\n", "1.1.0");
}

static ssize_t boot_state_show(struct device *dev,
                struct device_attribute *attr, char *buf)
{
    struct attiny *attiny = dev_get_drvdata(dev);
    uint8_t boot_state;

    boot_state_get(attiny, &boot_state);
    return sprintf(buf, "%d\n", boot_state);
}

static ssize_t boot_state_store(struct device *dev,
                struct device_attribute *attr,
                const char *buf, size_t count)
{
    struct attiny *attiny = dev_get_drvdata(dev);
    int inval;

    sscanf(buf, "%du", &inval);
    if(inval != 0){
        boot_state_clr(attiny);
    }
    return count;
}

static ssize_t fw_version_show(struct device *dev,
                struct device_attribute *attr, char *buf)
{
    attiny_cache_t *cache = ((struct attiny *)dev_get_drvdata(dev))->cache;
    return sprintf(buf, "%s\n", cache->fw_ver);
}

static ssize_t hw_version_show(struct device *dev,
                struct device_attribute *attr, char *buf)
{
    attiny_cache_t *cache = ((struct attiny *)dev_get_drvdata(dev))->cache;
    return sprintf(buf, "%s\n", cache->hw_ver);
}

static ssize_t product_name_show(struct device *dev,
                struct device_attribute *attr, char *buf)
{
    attiny_cache_t *cache = ((struct attiny *)dev_get_drvdata(dev))->cache;
    return sprintf(buf, "%s\n", cache->name);
}

static ssize_t product_type_show(struct device *dev,
                struct device_attribute *attr, char *buf)
{
    attiny_cache_t *cache = ((struct attiny *)dev_get_drvdata(dev))->cache;
    return sprintf(buf, "%s\n", cache->type);
}

static int strcompare(const char *str1, const char *str2)
{
    int result;
    size_t len1, len2;

    len1 = strlen(str1);
    len2 = strlen(str2);
    
    if (len1 < len2) {
        return -2; 
    } else if (len1 > len2) {
        return 2;
    }
    
    result = strncmp(str1, str2, len1);
    if (result < 0) {
        return -1;
    } else if (result > 0) {
        return 1;
    }
    
    return 0;
}

struct hw_ver {
    int major;
    int minor;
    int revision;
    int bom;
};

static int parse_uint(const char *str, size_t len, int *result) {
    int index;
    char pointer[20];
    long int res;
    *result = -1;
    
    for(index = 0; index < len && index < sizeof(pointer)-1; index++){
        if (isdigit(str[index])) {
            pointer[index] = str[index];
        } else {
            break;
        }
    }
    if (index == sizeof(pointer)-1 && isdigit(str[index])) {
        return -ERANGE;
    } else {
        pointer[index] = '\0';
    }
    
    if (kstrtol(pointer, 10, &res)) {
        return -EINVAL;
    } else if (res < 0 || res > INT_MAX) {
        return -ERANGE;
    }
    *result = res;
    return index;
}

static int parse_hw_ver(const char * str, struct hw_ver *version) {
    size_t len = strlen(str);
    size_t i = 0, j;
    long res;
    char pointer[20];
    if (len >= sizeof(pointer)) {
        return -EINVAL;
    }
    version->major = -1; version->minor = -1;
    version->revision = -1; version->bom = -1;
    
    // major
    if (i < len) {
        if ((res = parse_uint(&str[i], len - i, &version->major)) < 0){
            return res;
        }
        i += res;
    }
    
    // point between major and minor
    if (i < len && str[i++] != '.') {
        return -EINVAL;
    }
    //minor
    if (i < len) {
        if ((res = parse_uint(&str[i], len - i, &version->minor)) < 0){
            return res;
        }
        i += res;
    }
    //revision
    if (i < len) {
        if (!isalpha(str[i]) || (str[i+1] != '\0' && !isdigit(str[i+1]))) {
            return -EINVAL;
        }
        version->revision = (int)(tolower(str[i++])-'a');
    }
    // BOM
    if (i < len) {
        if ((res = parse_uint(&str[i], len - i, &version->bom)) < 0){
            return res;
        }
        i += res;
    }
    return 0;
}

static inline int int_compare(int int1, int int2) {
    if (int1 < int2) {
        return -1;
    } else if (int1 > int2) {
        return 1;
    } else {
        return 0;
    }
}

static int hw_ver_compare(struct hw_ver *ver1, struct hw_ver *ver2) {
    int cmp;
    // compare major
    if ((cmp = int_compare(ver1->major, ver2->major))){
        return cmp;
    }
    // compare minor
    if ((cmp = int_compare(ver1->minor, ver2->minor))){
        return cmp;
    }
    // compare revision
    if ((cmp = int_compare((int)ver1->revision, (int)ver2->revision))){
        return cmp;
    }
    // compare bom
    if ((cmp = int_compare(ver1->bom, ver2->bom))){
        return cmp;
    }
    return 0;
}

static ssize_t memory_ram_show(struct device *dev,
                struct device_attribute *attr, char *buf)
{
    attiny_cache_t *cache = ((struct attiny *)dev_get_drvdata(dev))->cache;
    if (!strcompare(cache->name, "LORIX One")) {
        return sprintf(buf, "%d\n", 128*1024);
    }
    return sprintf(buf, "unknown\n");
}

static ssize_t memory_nand_show(struct device *dev,
                struct device_attribute *attr, char *buf)
{
    struct hw_ver version; 
    attiny_cache_t *cache = ((struct attiny *)dev_get_drvdata(dev))->cache;

    if (!strcompare(cache->name, "LORIX One") && !parse_hw_ver(cache->hw_ver, &version)) {
        struct hw_ver ver_512MB = { .major = 1, .minor = 0, .revision = 3, .bom = 2};
        if (hw_ver_compare(&version, &ver_512MB) >= 0) {
            return sprintf(buf, "%d\n", 512*1024);
        } else {
            return sprintf(buf, "%d\n", 256*1024);
        }
    }
    return sprintf(buf, "unknown\n");
}

static DEVICE_ATTR(dev_version, S_IRUGO, dev_version_show, NULL);
static DEVICE_ATTR(boot_state, (S_IWUSR|S_IRUSR|S_IWGRP|S_IRGRP|S_IROTH), boot_state_show, boot_state_store);
static DEVICE_ATTR(fw_version, S_IRUGO, fw_version_show, NULL);
static DEVICE_ATTR(hw_version, S_IRUGO, hw_version_show, NULL);
static DEVICE_ATTR(product_name, S_IRUGO, product_name_show, NULL);
static DEVICE_ATTR(product_type, S_IRUGO, product_type_show, NULL);
static DEVICE_ATTR(mem_ram, S_IRUGO, memory_ram_show, NULL);
static DEVICE_ATTR(mem_nand, S_IRUGO, memory_nand_show, NULL);
static const struct attribute *machine_attrs[] = {
    &dev_attr_dev_version.attr,
    &dev_attr_boot_state.attr,
    &dev_attr_fw_version.attr,
    &dev_attr_hw_version.attr,
    &dev_attr_product_name.attr,
    &dev_attr_product_type.attr,
    &dev_attr_mem_ram.attr,
    &dev_attr_mem_nand.attr,
    NULL,
};
static const struct attribute_group machine_attr_group = {
    .attrs = (struct attribute **) machine_attrs,
};
static struct class *product_class;

static int pmic_lorix_probe(struct i2c_client *client,
                            const struct i2c_device_id *id)
{
    struct attiny_platform_data *pdata = dev_get_platdata(&client->dev);
    struct attiny *attiny;
    attiny_cache_t *cache;
    struct device *dev = &client->dev;
    int ret;
    uint8_t boot_state;

    /* Right now device-tree probed devices don't get dma_mask set.
     * Since shared usb code relies on it, set it here for now.
     * Once we have dma capability bindings this can go away.
     */
    ret = dma_coerce_mask_and_coherent(dev, DMA_BIT_MASK(32));
    if (ret)
        return ret;

    if (!i2c_check_functionality(client->adapter,
                    I2C_FUNC_SMBUS_BYTE_DATA)) {
        dev_err(dev, "SMBUS Word Data not Supported\n");
        return -EIO;
    }

    // creating the driver data
    attiny = devm_kzalloc(dev, sizeof(struct attiny), GFP_KERNEL);
    if (!attiny) {
        dev_err(dev, "Failed to allocate memory for driver data\n");
        return -ENOMEM;
    }

    // creating the cache driver data
    cache = devm_kzalloc(dev, sizeof(attiny_cache_t), GFP_KERNEL);
    if (!cache) {
        dev_err(dev, "Failed to allocate memory for driver cache data\n");
        return -ENOMEM;
    }
    attiny->cache = cache;

    // assign driver data to device
    dev_set_drvdata(dev, attiny);
    attiny->client = client;
    attiny->dev = dev;
    attiny->id = id->driver_data;

    // init the device lock
    mutex_init(&attiny->lock);

    if (pdata) {
        pmic_lorix_devs[0].platform_data = &pdata->leds;
        pmic_lorix_devs[0].pdata_size = sizeof(pdata->leds);
    } else {
        pmic_lorix_devs[0].platform_data = NULL;
        pmic_lorix_devs[0].pdata_size = 0;
    }
    ret = mfd_add_devices(dev, -1, (const struct mfd_cell *)&pmic_lorix_devs, 1, NULL, 0, NULL);
    if (ret < 0) {
        dev_err(dev, "add mfd devices failed: %d\n", ret);
        return ret;
    }

    // create product class which will contains the pmic driver access
    product_class = class_create(THIS_MODULE, "product");
    if (IS_ERR(product_class)) {
        dev_err(dev, "pmic-lorix cant create class %s\n", "product");
        ret = -ENODEV;
        goto out_remove_mfd;
    }

    // create machine hierarchy
    attiny->machine_dev = device_create(product_class, dev, 0, attiny, "machine");
    if (IS_ERR(attiny->machine_dev)) {
        dev_err(dev, "failed to create device '%s_%s'\n", "product", "machine");
        ret = -ENODEV;
        goto out_remove_class;
    }

    // create attribute group
    ret = sysfs_create_group(&attiny->machine_dev->kobj, &machine_attr_group);
    if (ret < 0) {
        dev_err(dev, "failed to create sysfs attributes group\n");
        goto out_remove_device;
    }

    // FW version, HW version and boot state work in all cases since revision 1.0c

    // read FW version
    ret = fw_version_get(attiny, cache->fw_ver);
    if (ret < 0) {
        dev_err(dev, "failed to retrieve FW version from pmic-lorix\n");
        goto out_remove_all;
    }

    // read HW version
    ret = hw_version_get(attiny, cache->hw_ver);
    if (ret < 0) {
        dev_err(dev, "failed to retrieve HW version from pmic-lorix\n");
        goto out_remove_all;
    }

    // read bootstate
    ret = boot_state_get(attiny, &boot_state);
    if (ret < 0) {
        dev_err(dev, "failed to retrieve boot_state from pmic-lorix\n");
        goto out_remove_all;
    }

    // values by default
    strcpy(cache->name, "LORIX One");
    strcpy(cache->type, "EU868");

    // test if features regs can be read
    ret = reg_feature1_get(attiny, &cache->feature1);
    if (ret < 0 || cache->feature1.bit.FEAT_LEGACY) {
        cache->feature1.bit.FEAT_BOOT = 1;
        cache->feature1.bit.FEAT_FW_VER = 1;
        cache->feature1.bit.FEAT_HW_VER = 1;
        cache->feature1.bit.FEAT_NAME = 0;
        cache->feature1.bit.FEAT_TYPE = 0;
        cache->feature1.bit.FEAT_PROG = 0;
    }

    // unused actually
    ret = reg_feature2_get(attiny, &cache->feature2);

    // read product name if possible
    if (cache->feature1.bit.FEAT_NAME) {
        ret = product_name_get(attiny, cache->name);
        if (ret < 0) {
            dev_err(dev, "failed to retrieve product name from pmic-lorix\n");
            goto out_remove_all;
        }
    }

    // read product type if possible
    if (cache->feature1.bit.FEAT_TYPE) {
        ret = product_type_get(attiny, cache->type);
        if (ret < 0) {
            dev_err(dev, "failed to retrieve product type from pmic-lorix\n");
            goto out_remove_all;
        }
    }

    // display machine info
    dev_info(dev, "Product %s detected\n", cache->name);
    dev_info(dev, "   Type: %s\n", cache->type);
    dev_info(dev, " HW ver: %s\n", cache->hw_ver);
    dev_info(dev, " FW ver: %s\n", cache->fw_ver);

    switch(boot_state){
    case 0x00:
        dev_info(dev, "   Boot: 0x00 (normal mode)\n");
        break;
    case 0x01:
        dev_info(dev, "   Boot: 0x01 (factory reset mode)\n");
        break;
    default:
        dev_info(dev, "   Boot: 0x%02X (unknown mode), clearing boot state\n", boot_state);
        boot_state_clr(attiny);
        break;
    }

    return 0;

out_remove_all:
    sysfs_remove_group(&attiny->machine_dev->kobj, &machine_attr_group);

out_remove_device:
    device_unregister(attiny->machine_dev);

out_remove_class:
    class_unregister(product_class);
    class_destroy(product_class);

out_remove_mfd:
    mfd_remove_devices(dev);

    return ret;
}

static int pmic_lorix_remove(struct i2c_client *client)
{
    struct attiny *attiny = dev_get_drvdata(&client->dev);
    sysfs_remove_group(&attiny->machine_dev->kobj, &machine_attr_group);
    device_unregister(attiny->machine_dev);
    class_unregister(product_class);
    class_destroy(product_class);
    mfd_remove_devices(&client->dev);
    return 0;
}

static const struct i2c_device_id pmic_lorix_id[] = {
    {"pmic-lorix", 0},
    {}
};
MODULE_DEVICE_TABLE(i2c, pmic_lorix_id);

#ifdef CONFIG_OF
static const struct of_device_id pmic_lorix_of_match[] = {
    { .compatible = "wifx,pmic-lorix", },
    {},
};
MODULE_DEVICE_TABLE(of, pmic_lorix_of_match);
#endif

static struct i2c_driver pmic_lorix_driver = {
    .probe        = pmic_lorix_probe,
    .remove        = pmic_lorix_remove,
    .driver = {
        .name    = "pmic-lorix",
        .of_match_table = of_match_ptr(pmic_lorix_of_match),
    },
    .id_table     = pmic_lorix_id,
};

static int __init pmic_lorix_i2c_init(void)
{
    return i2c_add_driver(&pmic_lorix_driver);
}
/* init early so consumer devices can complete system boot */
subsys_initcall(pmic_lorix_i2c_init);

static void __exit pmic_lorix_i2c_exit(void)
{
    i2c_del_driver(&pmic_lorix_driver);
}
module_exit(pmic_lorix_i2c_exit);

MODULE_AUTHOR("Yannick Lanz <yannick.lanz@wifx.net>");
MODULE_DESCRIPTION("LORIX One PMIC-MFD Driver");
MODULE_LICENSE("GPL");
