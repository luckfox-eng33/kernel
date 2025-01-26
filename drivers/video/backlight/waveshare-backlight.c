// SPDX-License-Identifier: GPL-2.0-only

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/fb.h>
#include <linux/i2c.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/pm.h>

#define MAX_BRIGHTNESS		0xff
#define INIT_BRIGHT			0x80

static int g_800_480_mode = 0;

struct waveshare_platform_data {
	const char *name;
	u16 initial_brightness;
};

struct waveshare_bl_data {
	struct device *dev;
	struct i2c_client *i2c;
	struct backlight_device *bl;
	struct waveshare_platform_data *pdata;
};

static int ws_set_brightness(struct waveshare_bl_data *data, u32 brightness)
{
	int ret;
	u8 val;

	if ( g_800_480_mode == TRUE) {
		val = brightness;

		ret = i2c_smbus_write_byte_data(data->i2c, 0x86, val);
		if (ret) {
			dev_err(data->dev, "I2C write failed: %d\n", ret);
			return ret;
		}

	} else {
		val = 0xff - brightness;
		ret = i2c_smbus_write_byte_data(data->i2c, 0xab, val);
		if (ret) {
			dev_err(data->dev, "I2C write failed: %d\n", ret);
			return ret;
		}

		ret = i2c_smbus_write_byte_data(data->i2c, 0xaa, 0x01);
		if (ret) {
			dev_err(data->dev, "I2C write failed: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static int ws_bl_update_status(struct backlight_device *bl)
{
	struct waveshare_bl_data *data = bl_get_data(bl);
	u32 brightness = bl->props.brightness;
	int ret;

	if (brightness > MAX_BRIGHTNESS)
		brightness = MAX_BRIGHTNESS;

	ret = ws_set_brightness(data, brightness);
	if (ret)
		return ret;

	return 0;
}

static const struct backlight_ops ws_bl_ops = {
	.update_status = ws_bl_update_status,
};

static int ws_create_backlight(struct waveshare_bl_data *data)
{
	struct backlight_properties *props;
	const char *name = data->pdata->name ? : "waveshare_bl";

	props = devm_kzalloc(data->dev, sizeof(*props), GFP_KERNEL);
	if (!props)
		return -ENOMEM;

	props->type = BACKLIGHT_PLATFORM;
	props->max_brightness = MAX_BRIGHTNESS;

	if (data->pdata->initial_brightness > props->max_brightness)
		data->pdata->initial_brightness = props->max_brightness;

	props->brightness = data->pdata->initial_brightness;

	data->bl = devm_backlight_device_register(data->dev, name, data->dev, data,
					&ws_bl_ops, props);

	return PTR_ERR_OR_ZERO(data->bl);
}

static void ws_parse_dt(struct waveshare_bl_data *data)
{
	struct device *dev = data->dev;
	struct device_node *node = dev->of_node;
	u32 prog_val;
	int ret;

	if(!node) {
		return;
	}

	ret = of_property_read_string(node, "label", &data->pdata->name);
	if (ret < 0)
		data->pdata->name = NULL;

	ret = of_property_read_u32(node, "default-brightness", &prog_val);
	if (ret == 0)
		data->pdata->initial_brightness = prog_val;
}

static int ws_backlight_probe(struct i2c_client *i2c,const struct i2c_device_id *id)
{
	struct waveshare_bl_data *data;
	int ret;

	data = devm_kzalloc(&i2c->dev, sizeof(*data), GFP_KERNEL);
	if(!data)
	return -ENOMEM;

	data->i2c = i2c;
	data->dev = &i2c->dev;
	data->pdata = dev_get_platdata(&i2c->dev);

	/* Check ID */
	ret = i2c_smbus_read_byte_data(i2c, 0x80);
	if (ret < 0)
		goto probe_err;

	switch (ret) {
	case 0xde: /* ver 1 */
	case 0xc3: /* ver 2*/
		g_800_480_mode = TRUE;
		break;
	default:
		g_800_480_mode = FALSE;
	}

	/* Init Screen */
	if (g_800_480_mode == TRUE) {
		ret = i2c_smbus_write_byte_data(i2c, 0x85, 0x00);
		if (ret)
			goto probe_err;
		msleep(800);

		ret = i2c_smbus_write_byte_data(i2c, 0x85, 0x01);
		if (ret)
			goto probe_err;
		msleep(800);

		ret = i2c_smbus_write_byte_data(i2c, 0x81, 0x04);
		if (ret)
			goto probe_err;
	} else {
		ret = i2c_smbus_write_byte_data(i2c, 0xc0, 0x01);
		if (ret)
			goto probe_err;

		ret = i2c_smbus_write_byte_data(i2c, 0xc2, 0x01);
		if (ret)
			goto probe_err;

		ret = i2c_smbus_write_byte_data(i2c, 0xac, 0x01);
		if (ret)
			goto probe_err;
	}

	/* Init Platform Data */
	if(!data->pdata) {
		data->pdata = devm_kzalloc(data->dev, sizeof(*data->pdata), GFP_KERNEL);
		if (!data->pdata)
			return -ENOMEM;

		data->pdata->name = NULL;
		data->pdata->initial_brightness = INIT_BRIGHT;

		if (IS_ENABLED(CONFIG_OF))
			ws_parse_dt(data);
	}

	i2c_set_clientdata(i2c, data);

	/* Set Initial Brightness */
	if (data->pdata->initial_brightness > MAX_BRIGHTNESS) {
		data->pdata->initial_brightness = MAX_BRIGHTNESS;
	}

	ret = ws_set_brightness(data, data->pdata->initial_brightness);
	if (ret)
		goto probe_err;

	/* Create Backlight */
	ret = ws_create_backlight(data);
	if (ret) {
		goto probe_err;
	}

	backlight_update_status(data->bl);

	/* Enable Panel */
	if(g_800_480_mode == FALSE) {
		ret = i2c_smbus_write_byte_data(i2c, 0xad, 0x01);
		if (ret)
			dev_err(data->dev, "I2C write failed: %d\n", ret);
	}

	return 0;

probe_err:
	dev_err(data->dev, "failed ret: %d\n", ret);
	return ret;
}

static void ws_backlight_remove(struct i2c_client *i2c)
{
	struct waveshare_bl_data *data = i2c_get_clientdata(i2c);

	data->bl->props.brightness = 0;
	backlight_update_status(data->bl);
}

static const struct of_device_id ws_backlight_of_ids[] = {
	{ .compatible = "waveshare,dsi-backlight", },
	{}
};
MODULE_DEVICE_TABLE(of, ws_backlight_of_ids);

static struct i2c_driver ws_backlight_driver = {
	.driver = {
		.name = "ws_backlight",
		.of_match_table = ws_backlight_of_ids,
	},
	.probe = ws_backlight_probe,
	.remove = ws_backlight_remove,
};
module_i2c_driver(ws_backlight_driver);

MODULE_AUTHOR("eng29 <eng29@luckfox.com>");
MODULE_DESCRIPTION("Waveshare DSI backlight driver");
MODULE_LICENSE("GPL");
