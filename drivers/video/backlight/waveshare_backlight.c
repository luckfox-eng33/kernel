// SPDX-License-Identifier: GPL-2.0-only
/*
 * Waveshare LED Driver
 */

#include <linux/backlight.h>
#include <linux/waveshare_backlight.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/fb.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/slab.h>

struct waveshare_bl_platform_data *bd_tp = NULL;

int waveshare_panel_read(struct waveshare_bl_platform_data *bd,
			 unsigned char reg, unsigned char *data)
{
	int ret = 0;
	// struct device *dev = &bd->client->dev;

	ret = i2c_smbus_read_byte_data(bd->client, reg);
	if (ret < 0) {
		// dev_err(dev, "i2c read error, reg: 0x%x \n", reg);
		ret = -1;
		return -1;
	}
	*data = ret;

	// printk("---------waveshare--------- waveshare_panel_read: reg = 0x%x, data = 0x%x \n", reg, *data);

	msleep(2);
	return 0;
}

int waveshare_panel_write(struct waveshare_bl_platform_data *bd,
			  unsigned char reg, unsigned char data)
{
	int ret = 0;
	// struct device *dev = &bd->client->dev;
	// printk("---------waveshare--------- waveshare_panel_write: reg = 0x%x, data = 0x%x \n", reg, data);

	ret = i2c_smbus_write_byte_data(bd->client, reg, data);
	if (ret < 0) {
		// dev_err(dev, "i2c write error, reg: 0x%x \n", reg);
		return -1;
	}
	msleep(2);
	return 0;
}

int waveshare_tp_rst(unsigned char status)
{
	int ret = 0;
	unsigned char read_data, write_data;

	if (bd_tp == NULL) {
		return -1;
	}

	ret = waveshare_panel_read(bd_tp, REG_TP, &read_data);
	if (ret != 0) {
		return -1;
	}

	if (status) {
		write_data = read_data | CFG_TP_RST;
		// printk("---------waveshare--------- waveshare_tp_rst[enable]: write_data = 0x%x \n", write_data);
		ret = waveshare_panel_write(bd_tp, REG_TP, write_data);
	} else {
		write_data = read_data & ~CFG_TP_RST;
		// printk("---------waveshare--------- waveshare_tp_rst[disable]: write_data = 0x%x \n", write_data);
		ret = waveshare_panel_write(bd_tp, REG_TP, write_data);
	}

	return 0;
}

int waveshare_bl_enable(struct waveshare_bl_platform_data *bd,
			unsigned char status)
{
	int ret = 0;
	unsigned char read_data, write_data;
	// struct device *dev = &bd->client->dev;

	ret = waveshare_panel_read(bd, REG_LCD, &read_data);
	if (ret != 0) {
		// dev_err(dev, "waveshare backlight enable set error [read err]\n");
		return -1;
	}

	if (status) {
		write_data = read_data | CFG_BL_EN;
		// printk("---------waveshare--------- waveshare_bl_enable[enable]: write_data = 0x%x \n", write_data);
		ret = waveshare_panel_write(bd, REG_LCD, write_data);
	} else {
		write_data = read_data & ~CFG_BL_EN;
		// printk("---------waveshare--------- waveshare_bl_enable[disable]: write_data = 0x%x \n", write_data);
		ret = waveshare_panel_write(bd, REG_LCD, write_data);
	}

	if (ret != 0) {
		// dev_err(dev, "waveshare backlight enable set error [write err]\n");
		return -1;
	}

	return 0;
}

static int waveshare_backlight_update_status(struct backlight_device *backlight)
{
	int ret = 0;
	unsigned char read_data;
	struct waveshare_bl_platform_data *bd = bl_get_data(backlight);
	int brightness = backlight_get_brightness(backlight);
	// struct device *dev = &bd->client->dev;

	ret = waveshare_panel_read(bd, REG_LCD, &read_data);
	if (ret != 0) {
		goto err;
	}

	if (read_data & CFG_BL_EN) {
		if (brightness == 0) {
			ret = waveshare_bl_enable(bd, 0);
		}
	} else {
		if (brightness > 0) {
			ret = waveshare_bl_enable(bd, 1);
		}
	}
	if (ret != 0) {
		goto err;
	}

	// printk("---------waveshare--------- waveshare_backlight_update_status = %d \n", brightness);

	if (bd->min_brightness > 0 && brightness > 0 &&
	    brightness < bd->min_brightness) {
		brightness = bd->min_brightness;
		// printk("---------waveshare--------- min_brightness = %d , brightness = %d\n", bd->min_brightness, brightness);
	}

	waveshare_panel_write(bd, REG_PWM, brightness);

	return 0;

err:
	// dev_err(dev, "waveshare backlight update status error\n");
	return 0;
}

static int waveshare_backlight_check_fb(struct backlight_device *backlight,
					struct fb_info *info)
{
	struct waveshare_bl_platform_data *bd = bl_get_data(backlight);

	return bd->fbdev == NULL || bd->fbdev == info->device;
}

static const struct backlight_ops waveshare_backlight_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.update_status = waveshare_backlight_update_status,
	.check_fb = waveshare_backlight_check_fb,
};

static int waveshare_bl_probe(struct i2c_client *client,
			      const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct backlight_device *backlight;
	struct backlight_properties props;
	struct waveshare_bl_platform_data *bd;
	int ret;
	unsigned char read_data, write_data;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_warn(&client->dev,
			 "I2C adapter doesn't support I2C_FUNC_SMBUS_BYTE\n");
		return -EIO;
	}

	bd = devm_kzalloc(&client->dev, sizeof(*bd), GFP_KERNEL);
	if (!bd)
		return -ENOMEM;

	bd->client = client;

	bd_tp = bd;

	ret = waveshare_panel_read(bd, REG_ID, &read_data);
	if (ret == 0) {
		dev_info(dev, "hw id = 0x%x\n", read_data);
	}

	ret = waveshare_panel_read(bd, REG_SIZE, &read_data);
	if (ret == 0) {
		dev_info(dev, "panel size = %d\n", read_data);
	}

	ret = waveshare_panel_read(bd, REG_VERSION, &read_data);
	if (ret == 0) {
		dev_info(dev, "panel mcu version = 0x%x\n", read_data);
	}

	ret = waveshare_panel_read(bd, REG_LCD, &read_data);
	if (ret == 0) {
		write_data = read_data | CFG_VCC_EN;
		waveshare_panel_write(bd, REG_LCD, write_data);
		msleep(10);
		write_data = write_data | CFG_LCD_PWR;
		waveshare_panel_write(bd, REG_LCD, write_data);
	}

	ret = waveshare_panel_read(bd, REG_TP, &read_data);
	if (ret == 0) {
		write_data = read_data | CFG_TP_PWR;
		waveshare_panel_write(bd, REG_TP, write_data);
	}

	ret = of_property_read_u32(dev->of_node, "default-brightness-level",
				   &bd->def_brightness);
	if (ret) {
		dev_err(dev, "could not get default-brightness-level\n");
		goto err;
	}

	ret = of_property_read_u32(dev->of_node, "max-brightness-level",
				   &bd->max_brightness);
	if (ret) {
		dev_err(dev, "could not get max-brightness-level\n");
		goto err;
	}

	ret = of_property_read_u32(dev->of_node, "min-brightness-level",
				   &bd->min_brightness);
	if (ret) {
		dev_err(dev, "could not get min-brightness-level,default 0\n");
		bd->min_brightness = 0;
	}

	// printk("---------waveshare--------- max-brightness-level = %d,  default-brightness-level = %d  \n", bd->max_brightness, bd->def_brightness);

	memset(&props, 0, sizeof(props));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = bd->max_brightness;
	backlight = devm_backlight_device_register(&client->dev, "backlight",
						   &bd->client->dev, bd,
						   &waveshare_backlight_ops,
						   &props);
	if (IS_ERR(backlight)) {
		dev_err(&client->dev, "failed to register backlight\n");
		return PTR_ERR(backlight);
	}

	if (bd->def_brightness > bd->max_brightness) {
		dev_warn(&client->dev,
			 "invalid default brightness level: %u, using %u\n",
			 bd->def_brightness, bd->max_brightness);
		bd->def_brightness = bd->max_brightness;
	}
	backlight->props.brightness = bd->def_brightness;

	backlight_update_status(backlight);
	i2c_set_clientdata(client, backlight);

	return 0;

err:
	dev_err(dev, "waveshare backlight probe error\n");
	return -1;
}

static void waveshare_bl_remove(struct i2c_client *client)
{
	struct backlight_device *backlight = i2c_get_clientdata(client);

	backlight->props.brightness = 0;
	backlight_update_status(backlight);
	backlight_device_unregister(backlight);
}

#ifdef CONFIG_OF
static const struct of_device_id waveshare_bl_of_match[] = {
	{
		.compatible = "waveshare,waveshare-backlight",
	},
	{}
};
MODULE_DEVICE_TABLE(of, waveshare_bl_of_match);
#endif

static const struct i2c_device_id waveshare_bl_ids[] = {
	{ "waveshare-backlight", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, waveshare_bl_ids);

static struct i2c_driver waveshare_bl_driver = {
	.driver = {
		.name = "waveshare-backlight",
		.of_match_table = of_match_ptr(waveshare_bl_of_match),
	},
	.probe = waveshare_bl_probe,
	.remove = waveshare_bl_remove,
	.id_table = waveshare_bl_ids,
};

module_i2c_driver(waveshare_bl_driver);

MODULE_DESCRIPTION("Waveshare Backlight Driver");
MODULE_AUTHOR("luckfox_eng33 <eng33@luckfox.com>");
MODULE_LICENSE("GPL");
