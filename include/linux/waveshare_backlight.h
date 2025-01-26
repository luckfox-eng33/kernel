/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _WAVESHARE_BACKLIGHT_H
#define _WAVESHARE_BACKLIGHT_H

/* I2C registers of the waveshare panel controller. */
#define REG_TP              0x94
#define REG_LCD             0x95
#define REG_PWM             0x96
#define REG_SIZE            0x97
#define REG_ID              0x98
#define REG_VERSION         0x99

#define CFG_TP_INT           BIT(2)
#define CFG_TP_RST           BIT(1)
#define CFG_TP_PWR           BIT(0)

#define CFG_VCC_EN           BIT(4)
#define CFG_BL_EN            BIT(2)
#define CFG_LCD_RST          BIT(1)
#define CFG_LCD_PWR          BIT(0)

struct waveshare_bl_platform_data {
	struct i2c_client *client;
	// struct backlight_device *backlight;
	struct device *fbdev;
	unsigned int def_brightness;
	unsigned int max_brightness;
	unsigned int min_brightness;
};

extern int waveshare_panel_read(struct waveshare_bl_platform_data *bd, unsigned char reg, unsigned char *data);
extern int waveshare_panel_write(struct waveshare_bl_platform_data *bd, unsigned char reg, unsigned char data);
extern int waveshare_tp_rst(unsigned char status);

#endif