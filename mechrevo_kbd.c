// SPDX-License-Identifier: GPL-2.0
/*
 * RGB keyboard backlight for MECHREVO laptops built on Uniwill barebones
 * (tested on WUJIE15XA / GX5HRXG). Other Uniwill-based laptops can be tried
 * with force=1; the EC must still report a single-zone RGB keyboard.
 *
 * The EC is reached through the ACPI methods ECRR/ECRW of \_SB.INOU, the same
 * way the mainline uniwill-laptop driver does it. Register layout and the
 * apply sequence are taken from that driver (keyboard backlight support was
 * added after 7.0).
 */

#include <linux/acpi.h>
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/dmi.h>
#include <linux/led-class-multicolor.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>

#define DRIVER_NAME		"mechrevo_kbd"

#define EC_ADDR_BAREBONE_ID	0x0740
#define EC_ADDR_SUPPORT_2	0x0766
#define RGB_KEYBOARD		BIT(2)

#define EC_ADDR_TRIGGER		0x0767
#define RGB_APPLY_COLOR		BIT(5)
#define RGB_RAINBOW_EFFECT	BIT(7)

#define EC_ADDR_RGB_RED		0x0769
#define EC_ADDR_RGB_GREEN	0x076A
#define EC_ADDR_RGB_BLUE	0x076B

#define EC_ADDR_KBD_STATUS	0x078C
#define KBD_POWER_OFF		BIT(1)
#define KBD_APPLY		BIT(4)
#define KBD_BRIGHTNESS_MASK	GENMASK(7, 5)

#define EC_DELAY_US		6000

#define KBD_CHANNELS		3
#define KBD_EC_MAX_INTENSITY	50	/* EC scale */
#define KBD_MAX_INTENSITY	255	/* sysfs scale */
#define KBD_MAX_BRIGHTNESS	4

static bool force;
module_param(force, bool, 0444);
MODULE_PARM_DESC(force, "Load on non-MECHREVO Uniwill-based laptops");

static acpi_handle ec_handle;
static struct platform_device *pdev;
static struct led_classdev_mc mc_cdev;
static struct mc_subled subleds[KBD_CHANNELS];
static DEFINE_MUTEX(ec_lock);
static bool rainbow;

/* Last color sent to the EC, so animations only pay for what changed */
static u8 last_ec_val[KBD_CHANNELS];
static bool last_ec_val_valid;

static const struct dmi_system_id mechrevo_dmi_table[] = {
	{ .matches = { DMI_MATCH(DMI_SYS_VENDOR, "MECHREVO") } },
	{ }
};
MODULE_DEVICE_TABLE(dmi, mechrevo_dmi_table);

static const u16 channel_reg[KBD_CHANNELS] = {
	EC_ADDR_RGB_RED, EC_ADDR_RGB_GREEN, EC_ADDR_RGB_BLUE,
};

static int uw_ec_read(u16 reg, u8 *val)
{
	union acpi_object param = {
		.integer = { .type = ACPI_TYPE_INTEGER, .value = reg },
	};
	struct acpi_object_list input = { .count = 1, .pointer = &param };
	unsigned long long out;
	acpi_status status;

	status = acpi_evaluate_integer(ec_handle, "ECRR", &input, &out);
	if (ACPI_FAILURE(status))
		return -EIO;
	if (out > U8_MAX)
		return -ENXIO;

	usleep_range(EC_DELAY_US, EC_DELAY_US * 2);
	*val = out;
	return 0;
}

static int uw_ec_write(u16 reg, u8 val)
{
	union acpi_object params[2] = {
		{ .integer = { .type = ACPI_TYPE_INTEGER, .value = reg } },
		{ .integer = { .type = ACPI_TYPE_INTEGER, .value = val } },
	};
	struct acpi_object_list input = { .count = 2, .pointer = params };
	acpi_status status;

	status = acpi_evaluate_object(ec_handle, "ECRW", &input, NULL);
	if (ACPI_FAILURE(status))
		return -EIO;

	usleep_range(EC_DELAY_US, EC_DELAY_US * 2);
	return 0;
}

static int uw_ec_update_bits(u16 reg, u8 mask, u8 val)
{
	u8 old;
	int ret;

	ret = uw_ec_read(reg, &old);
	if (ret)
		return ret;

	/* Always write, the EC acts on the apply bits even if unchanged */
	return uw_ec_write(reg, (old & ~mask) | (val & mask));
}

static int write_brightness(unsigned int brightness)
{
	/* KBD_POWER_OFF is cleared, KBD_APPLY must always be written */
	return uw_ec_update_bits(EC_ADDR_KBD_STATUS,
			      KBD_BRIGHTNESS_MASK | KBD_APPLY | KBD_POWER_OFF,
			      FIELD_PREP(KBD_BRIGHTNESS_MASK, brightness) | KBD_APPLY);
}

/* Caller holds ec_lock */
static int sync_brightness_locked(unsigned int brightness)
{
	u8 status;
	int ret;

	/* Fn keys change brightness behind our back, so compare with the EC, not a cache */
	ret = uw_ec_read(EC_ADDR_KBD_STATUS, &status);
	if (ret)
		return ret;
	if (FIELD_GET(KBD_BRIGHTNESS_MASK, status) == brightness && !(status & KBD_POWER_OFF))
		return 0;

	return write_brightness(brightness);
}

/* Caller holds ec_lock */
static int apply_color_locked(unsigned int brightness)
{
	u8 ec_val[KBD_CHANNELS];
	int ret;

	for (int i = 0; i < KBD_CHANNELS; i++)
		ec_val[i] = min(subleds[i].intensity, KBD_MAX_INTENSITY) *
			    KBD_EC_MAX_INTENSITY / KBD_MAX_INTENSITY;

	/* EC treats 0x000000 as "restore default color", send faint white instead */
	if (!ec_val[0] && !ec_val[1] && !ec_val[2])
		memset(ec_val, 1, sizeof(ec_val));

	for (int i = 0; i < KBD_CHANNELS; i++) {
		if (!rainbow && last_ec_val_valid && ec_val[i] == last_ec_val[i])
			continue;
		ret = uw_ec_write(channel_reg[i], ec_val[i]);
		if (ret)
			return ret;
		last_ec_val[i] = ec_val[i];
	}
	last_ec_val_valid = true;

	/* Applying a color also ends the hardware rainbow */
	ret = uw_ec_update_bits(EC_ADDR_TRIGGER, RGB_APPLY_COLOR | RGB_RAINBOW_EFFECT,
				RGB_APPLY_COLOR);
	if (ret)
		return ret;
	rainbow = false;

	return sync_brightness_locked(brightness);
}

static int kbd_brightness_set(struct led_classdev *led_cdev,
			      enum led_brightness brightness)
{
	guard(mutex)(&ec_lock);

	/* While the EC runs the rainbow, only brightness is ours to change */
	if (rainbow)
		return sync_brightness_locked(brightness);

	return apply_color_locked(brightness);
}

static ssize_t rainbow_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", rainbow);
}

static ssize_t rainbow_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	bool enable;
	int ret;

	ret = kstrtobool(buf, &enable);
	if (ret)
		return ret;

	guard(mutex)(&ec_lock);

	if (enable) {
		/* The EC cycles colors on its own until a color is applied again */
		ret = uw_ec_update_bits(EC_ADDR_TRIGGER, RGB_RAINBOW_EFFECT, RGB_RAINBOW_EFFECT);
		if (!ret)
			rainbow = true;
	} else if (rainbow) {
		ret = apply_color_locked(mc_cdev.led_cdev.brightness);
	}

	return ret ?: count;
}

/* The multicolor class owns the LED's attribute groups, so extras live on the parent */
static DEVICE_ATTR_RW(rainbow);

static struct attribute *kbd_attrs[] = {
	&dev_attr_rainbow.attr,
	NULL
};
ATTRIBUTE_GROUPS(kbd);

static enum led_brightness kbd_brightness_get(struct led_classdev *led_cdev)
{
	u8 val;

	guard(mutex)(&ec_lock);

	if (uw_ec_read(EC_ADDR_KBD_STATUS, &val))
		return led_cdev->brightness;

	return min_t(u8, FIELD_GET(KBD_BRIGHTNESS_MASK, val), KBD_MAX_BRIGHTNESS);
}

static int __init mechrevo_kbd_init(void)
{
	struct led_init_data init_data = {
		.default_label = "rgb:" LED_FUNCTION_KBD_BACKLIGHT,
		.devicename = DRIVER_NAME,
		.devname_mandatory = true,
	};
	static const unsigned int color_ids[KBD_CHANNELS] = {
		LED_COLOR_ID_RED, LED_COLOR_ID_GREEN, LED_COLOR_ID_BLUE,
	};
	u8 barebone, support2, status, rgb[KBD_CHANNELS];
	int ret;

	if (!dmi_check_system(mechrevo_dmi_table) && !force)
		return -ENODEV;

	if (ACPI_FAILURE(acpi_get_handle(NULL, "\\_SB.INOU", &ec_handle)) ||
	    !acpi_has_method(ec_handle, "ECRR") || !acpi_has_method(ec_handle, "ECRW")) {
		pr_err(DRIVER_NAME ": \\_SB.INOU.ECRR/ECRW not found\n");
		return -ENODEV;
	}

	/* Read-only probe first, nothing is written before the LED is used */
	ret = uw_ec_read(EC_ADDR_BAREBONE_ID, &barebone) ?:
	      uw_ec_read(EC_ADDR_SUPPORT_2, &support2) ?:
	      uw_ec_read(EC_ADDR_KBD_STATUS, &status);
	for (int i = 0; !ret && i < KBD_CHANNELS; i++)
		ret = uw_ec_read(channel_reg[i], &rgb[i]);
	if (ret) {
		pr_err(DRIVER_NAME ": EC read failed: %d\n", ret);
		return ret;
	}

	pr_info(DRIVER_NAME ": barebone=%#04x support2=%#04x kbd_status=%#04x rgb=%u,%u,%u\n",
		barebone, support2, status, rgb[0], rgb[1], rgb[2]);

	if (!(support2 & RGB_KEYBOARD)) {
		pr_err(DRIVER_NAME ": EC does not report a single-zone RGB keyboard\n");
		return -ENODEV;
	}

	pdev = platform_device_alloc(DRIVER_NAME, PLATFORM_DEVID_NONE);
	if (!pdev)
		return -ENOMEM;
	/* Set before adding, so the files exist when udev sees the device */
	pdev->dev.groups = kbd_groups;
	ret = platform_device_add(pdev);
	if (ret) {
		platform_device_put(pdev);
		return ret;
	}

	for (int i = 0; i < KBD_CHANNELS; i++) {
		subleds[i].color_index = color_ids[i];
		subleds[i].intensity = min_t(u8, rgb[i], KBD_EC_MAX_INTENSITY) *
				       KBD_MAX_INTENSITY / KBD_EC_MAX_INTENSITY;
		subleds[i].channel = i;
	}

	mc_cdev.subled_info = subleds;
	mc_cdev.num_colors = KBD_CHANNELS;
	mc_cdev.led_cdev.max_brightness = KBD_MAX_BRIGHTNESS;
	mc_cdev.led_cdev.brightness = min_t(u8, FIELD_GET(KBD_BRIGHTNESS_MASK, status),
					    KBD_MAX_BRIGHTNESS);
	mc_cdev.led_cdev.color = LED_COLOR_ID_RGB;
	mc_cdev.led_cdev.flags = LED_REJECT_NAME_CONFLICT | LED_RETAIN_AT_SHUTDOWN;
	mc_cdev.led_cdev.brightness_set_blocking = kbd_brightness_set;
	mc_cdev.led_cdev.brightness_get = kbd_brightness_get;

	ret = led_classdev_multicolor_register_ext(&pdev->dev, &mc_cdev, &init_data);
	if (ret) {
		platform_device_unregister(pdev);
		return ret;
	}

	return 0;
}

static void __exit mechrevo_kbd_exit(void)
{
	led_classdev_multicolor_unregister(&mc_cdev);
	platform_device_unregister(pdev);
}

module_init(mechrevo_kbd_init);
module_exit(mechrevo_kbd_exit);

MODULE_DESCRIPTION("RGB keyboard backlight for MECHREVO / Uniwill laptops");
MODULE_LICENSE("GPL");
