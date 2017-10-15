#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/leds.h>
#include <linux/dmi.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>
#include <linux/efi.h>
#include <linux/suspend.h>
#include <linux/highmem.h>
#include <linux/capability.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/uio.h>
#include <linux/uaccess.h>
#include <acpi/apei.h>

#define ACPI_KEYBOARD_OP_TARGET 0xCA1CDF18
#define ACPI_KEYBOARD_OP_LENGTH 0x00D0

#define ACPI_KEYBOARD_OP_KBTO 0x7C
#define ACPI_KEYBOARD_OP_KBST 0x8E
#define ACPI_KEYBOARD_OP_KBLL 0x8F


// pci_read_config_byte
//
//
// SNVS
// SECS

#define SAMSUNG_ACPI_FN_AUTO "\\_SB.PCI0.LPCB.H_EC._Q70"
#define SAMSUNG_ACPI_REGION "\\SNVS"
#define SAMSUNG_ACPI_FN "\\SECS"

#define ACPI_KBD_FN_ON 0x9A
#define ACPI_KBD_FN_OFF 0x9B

// SECH
// SECB
// SECW

struct samsung_kb_led {
	const char * name;

};

struct samsung_laptop {
	struct mutex samsung_mutex;

	struct platform_device * platform_device;

	unsigned char * acpi_mem;
	acpi_handle acpi_fn;
	acpi_handle acpi_fn_auto;

	struct led_classdev kbd_led;

	unsigned char reg_led_auto;
	unsigned char reg_led_auto_state;

	unsigned char sr_brightness;
	unsigned char sr_auto;
};

//
// LEDS
//

static void kbd_led_kb_fn_auto ( struct samsung_laptop * samsung )
{
	acpi_status status;
	struct acpi_object_list input;

	input.count = 0;

	status = acpi_evaluate_object ( samsung->acpi_fn_auto, NULL, &input, NULL );

	if ( ACPI_FAILURE ( status ) )
		pr_err ( "ACPI update backlight auto fail!\n" );
}

static void kbd_led_kb_fn ( struct samsung_laptop * samsung, unsigned char on )
{
	acpi_status status;
	union acpi_object param[1];
	struct acpi_object_list input;

	param[0].type = ACPI_TYPE_INTEGER;
	param[0].integer.value = ( on ? ACPI_KBD_FN_ON : ACPI_KBD_FN_OFF );
	input.count = 1;
	input.pointer = param;

	status = acpi_evaluate_object ( samsung->acpi_fn, NULL, &input, NULL );

	if ( ACPI_FAILURE ( status ) )
		pr_err ( "ACPI update backlight fail!\n" );
}

static void kbd_led_kb_set ( struct samsung_laptop * samsung, unsigned char brightness, unsigned char sauto )
{
	unsigned char cur_value;
	unsigned char cur_brightness;
	unsigned char cur_auto;

	cur_value = * ( samsung->acpi_mem + ACPI_KEYBOARD_OP_KBLL );
	cur_brightness = cur_value & 0b00000111;
	cur_auto = ( * ( samsung->acpi_mem + ACPI_KEYBOARD_OP_KBTO ) ? 0 : 1 );

	if ( brightness & 0x80 ) {
		cur_brightness = brightness & ~0x80;
	}

	if ( sauto & 0x80 ) {
		cur_auto = sauto & ~0x80;
	}

	if ( cur_brightness > 4 ) {
		cur_brightness = 4;
	}

	samsung->kbd_led.brightness = cur_brightness;

	cur_auto = cur_auto ? 1 : 0;

	* ( samsung->acpi_mem + ACPI_KEYBOARD_OP_KBLL ) = cur_brightness;
	* ( samsung->acpi_mem + ACPI_KEYBOARD_OP_KBTO ) = ( cur_auto ? 0 : 1 );

	kbd_led_kb_fn ( samsung, cur_brightness );

	if ( ! ( samsung->kbd_led.flags & LED_SUSPENDED ) ) {
		samsung->sr_brightness = cur_brightness;
		samsung->sr_auto = cur_auto;
	}

	if ( !cur_auto ) {
		kbd_led_kb_fn_auto ( samsung );
	}
}

//
// STATE
//
static ssize_t auto_state_show ( struct device * dev, struct device_attribute * attr, char * buf )
{
	struct samsung_laptop * samsung;
	unsigned char state;
	struct led_classdev * led_cdev = dev_get_drvdata ( dev );
	samsung = container_of ( led_cdev, struct samsung_laptop, kbd_led );

	state = * ( samsung->acpi_mem + ACPI_KEYBOARD_OP_KBST );

	return sprintf ( buf, "%u\n", ( state ? 1 : 0 ) );
}
static DEVICE_ATTR_RO ( auto_state );

//
// auto
//
static ssize_t auto_store ( struct device * dev, struct device_attribute * attr, const char * buf, size_t size )
{
	struct samsung_laptop * samsung;
	unsigned long state;
	ssize_t ret;
	struct led_classdev * led_cdev = dev_get_drvdata ( dev );
	samsung = container_of ( led_cdev, struct samsung_laptop, kbd_led );

	mutex_lock ( &led_cdev->led_access );

	if ( led_sysfs_is_disabled ( led_cdev ) ) {
		ret = -EBUSY;
		goto unlock;
	}

	ret = kstrtoul ( buf, 10, &state );

	if ( ret )
		goto unlock;

	if ( state )
		state = 1;
	else
		state = 0;

	kbd_led_kb_set ( samsung, 0, state | 0x80 );

	ret = size;

unlock:
	mutex_unlock ( &led_cdev->led_access );
	return ret;
}
static ssize_t auto_show ( struct device * dev, struct device_attribute * attr, char * buf )
{
	struct samsung_laptop * samsung;
	unsigned char state;
	struct led_classdev * led_cdev = dev_get_drvdata ( dev );
	samsung = container_of ( led_cdev, struct samsung_laptop, kbd_led );

	state = * ( samsung->acpi_mem + ACPI_KEYBOARD_OP_KBTO );

	return sprintf ( buf, "%u\n", ( state ? 0 : 1 ) );
}
static DEVICE_ATTR_RW ( auto );


//
// LEVEL
//
static void kbd_led_set ( struct led_classdev * led_cdev, enum led_brightness value )
{
	struct samsung_laptop * samsung;

	samsung = container_of ( led_cdev, struct samsung_laptop, kbd_led );

	if ( value > samsung->kbd_led.max_brightness )
		value = samsung->kbd_led.max_brightness;
	else if ( value < 0 )
		value = 0;

	samsung->kbd_led.brightness = value;

	kbd_led_kb_set ( samsung, value | 0x80,  0 );
}

static enum led_brightness kbd_led_get ( struct led_classdev * led_cdev )
{
	struct samsung_laptop * samsung;
	unsigned char cur_level;
	samsung = container_of ( led_cdev, struct samsung_laptop, kbd_led );

	cur_level = ( * ( samsung->acpi_mem + ACPI_KEYBOARD_OP_KBLL ) ) & 0b00000111;

	if ( cur_level > 4 )
		cur_level = 4;

	return cur_level;
}

static void samsung_leds_exit ( struct samsung_laptop * samsung )
{
	if ( !IS_ERR_OR_NULL ( samsung->kbd_led.dev ) ) {
		if ( samsung->reg_led_auto_state ) {
			device_remove_file ( samsung->kbd_led.dev, &dev_attr_auto_state );
		}

		if ( samsung->reg_led_auto ) {
			device_remove_file ( samsung->kbd_led.dev, &dev_attr_auto );
		}

		led_classdev_unregister ( &samsung->kbd_led );
	}
}

static void samsung_leds_resume ( struct led_classdev * led_cdev )
{
	struct samsung_laptop * samsung;
	samsung = container_of ( led_cdev, struct samsung_laptop, kbd_led );

	kbd_led_kb_set ( samsung, samsung->sr_brightness | 0x80, samsung->sr_auto | 0x80 );
}

static int __init samsung_leds_init ( struct samsung_laptop * samsung )
{
	int ret = 0;

	samsung->kbd_led.name = "samsung::kbd_backlight";
	samsung->kbd_led.brightness_set = kbd_led_set;
	samsung->kbd_led.brightness_get = kbd_led_get;
	samsung->kbd_led.max_brightness = 4;
	samsung->kbd_led.flags |= LED_CORE_SUSPENDRESUME;
	samsung->kbd_led.flash_resume = samsung_leds_resume;

	ret = led_classdev_register ( &samsung->platform_device->dev, &samsung->kbd_led );

	if ( ret )
		goto error_leds_init;

	ret = device_create_file ( samsung->kbd_led.dev, &dev_attr_auto );

	if ( ret )
		goto error_leds_init;

	samsung->reg_led_auto = 1;

	ret = device_create_file ( samsung->kbd_led.dev, &dev_attr_auto_state );

	if ( ret )
		goto error_leds_init;

	samsung->reg_led_auto_state = 1;

	samsung_leds_resume ( &samsung->kbd_led );

	return 0;
error_leds_init:
	samsung_leds_exit ( samsung );
	return ret;
}

//
// SYSFS
//

static umode_t samsung_sysfs_is_visible ( struct kobject * kobj, struct attribute * attr, int idx )
{
	struct device * dev = container_of ( kobj, struct device, kobj );
	struct platform_device * pdev = to_platform_device ( dev );
	struct samsung_laptop * samsung = platform_get_drvdata ( pdev );
	bool ok = true;

	// if (attr == &dev_attr_performance_level.attr)
	// 	ok = !!samsung->config->performance_levels[0].name;
	// if (attr == &dev_attr_battery_life_extender.attr)
	// 	ok = !!(read_battery_life_extender(samsung) >= 0);
	// if (attr == &dev_attr_usb_charge.attr)
	// 	ok = !!(read_usb_charge(samsung) >= 0);
	// if (attr == &dev_attr_lid_handling.attr)
	// 	ok = !!(read_lid_handling(samsung) >= 0);
	//
	return ok ? attr->mode : 0;
}

static struct attribute * platform_attributes[] = {
	// &dev_attr_performance_level.attr,
	// &dev_attr_battery_life_extender.attr,
	// &dev_attr_usb_charge.attr,
	// &dev_attr_lid_handling.attr,
	NULL
};

static const struct attribute_group platform_attribute_group = {
	.is_visible = samsung_sysfs_is_visible,
	.attrs = platform_attributes
};


static int __init samsung_sysfs_init ( struct samsung_laptop * samsung )
{
	struct platform_device * device = samsung->platform_device;

	return sysfs_create_group ( &device->dev.kobj, &platform_attribute_group );

}

static void samsung_sysfs_exit ( struct samsung_laptop * samsung )
{
	struct platform_device * device = samsung->platform_device;

	sysfs_remove_group ( &device->dev.kobj, &platform_attribute_group );
}

static int __init samsung_acpi_region_init ( struct samsung_laptop * samsung )
{
	acpi_status status;
	acpi_handle handle;

	status = acpi_get_handle ( NULL, SAMSUNG_ACPI_REGION, &handle );

	if ( ACPI_FAILURE ( status ) )
		return status;

	status = acpi_get_handle ( NULL, SAMSUNG_ACPI_FN, &samsung->acpi_fn );

	if ( ACPI_FAILURE ( status ) )
		return status;

	status = acpi_get_handle ( NULL, SAMSUNG_ACPI_FN_AUTO, &samsung->acpi_fn_auto );

	if ( ACPI_FAILURE ( status ) )
		return status;

	samsung->acpi_mem = acpi_os_map_memory ( ACPI_KEYBOARD_OP_TARGET, ACPI_KEYBOARD_OP_LENGTH );

	if ( !samsung->acpi_mem )
		return -ENOMEM;

	return 0;
}

static void samsung_acpi_region_exit ( struct samsung_laptop * samsung )
{
	if ( samsung->acpi_mem )
		acpi_os_unmap_memory ( samsung->acpi_mem, ACPI_KEYBOARD_OP_LENGTH );
}

static int __init samsung_platform_init ( struct samsung_laptop * samsung )
{
	struct platform_device * pdev;

	pdev = platform_device_register_simple ( "samsung", -1, NULL, 0 );

	if ( IS_ERR ( pdev ) )
		return PTR_ERR ( pdev );

	samsung->platform_device = pdev;
	platform_set_drvdata ( samsung->platform_device, samsung );

	return 0;
};


static void samsung_platform_exit ( struct samsung_laptop * samsung )
{

	if ( samsung->platform_device ) {
		platform_device_unregister ( samsung->platform_device );
		samsung->platform_device = NULL;
	}
}

static struct dmi_system_id __initconst samsung_dmi_table[] = {
	{
		.ident = "SAMSUNG ELECTRONICS CO., LTD.",
		.matches = {
			DMI_MATCH ( DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD." ),
			DMI_MATCH ( DMI_PRODUCT_NAME, "870Z5E/880Z5E/680Z5E" ),
			DMI_MATCH ( DMI_BOARD_NAME, "NP870Z5E-X01RU" ),
		},
	},
	{ }
};
MODULE_DEVICE_TABLE ( dmi, samsung_dmi_table );

static struct platform_device * samsung_platform_device;

static int __init samsung_init ( void )
{
	struct samsung_laptop * samsung;
	int ret;

	if ( !efi_enabled ( EFI_BOOT ) ) {
		pr_err ( "!EFI_BOOT" );
		return -ENODEV;
	}

	if ( !dmi_check_system ( samsung_dmi_table ) ) {
		pr_err ( "!dmi_check_system" );
		return -ENODEV;
	}

	samsung = kzalloc ( sizeof ( *samsung ), GFP_KERNEL );

	if ( !samsung )
		return -ENOMEM;

	samsung->reg_led_auto = 0;
	samsung->reg_led_auto_state = 0;

	samsung->sr_brightness = 4;
	samsung->sr_auto = 1;

	mutex_init ( &samsung->samsung_mutex );

	ret = samsung_platform_init ( samsung );

	if ( ret )
		goto error_platform;


	ret = samsung_acpi_region_init ( samsung );

	if ( ret )
		goto error_acpi_region;

	// ret = samsung_sabi_init(samsung);
	// if (ret)
	// goto error_sabi;

	ret = samsung_sysfs_init ( samsung );

	if ( ret )
		goto error_sysfs;

	// ret = samsung_backlight_init ( samsung );

	// if ( ret )
	// 	goto error_backlight;

	// ret = samsung_rfkill_init ( samsung );

	// if ( ret )
	// 	goto error_rfkill;

	ret = samsung_leds_init ( samsung );

	if ( ret )
		goto error_leds;

	samsung_platform_device = samsung->platform_device;

	return ret;

	// error_debugfs:
	// samsung_lid_handling_exit ( samsung );
	// error_lid_handling:
	samsung_leds_exit ( samsung );
error_leds:
	// samsung_rfkill_exit ( samsung );
	// error_rfkill:
	// samsung_backlight_exit ( samsung );
	// error_backlight:
	samsung_sysfs_exit ( samsung );
error_sysfs:
	samsung_acpi_region_exit ( samsung );
error_acpi_region:
	samsung_platform_exit ( samsung );
error_platform:
	kfree ( samsung );
	return ret;
}

static void __exit samsung_exit ( void )
{
	struct samsung_laptop * samsung;
	samsung = platform_get_drvdata ( samsung_platform_device );
	kbd_led_kb_set ( samsung, 0x80, 0x80 );
	// samsung_debugfs_exit(samsung);
	// samsung_lid_handling_exit(samsung);
	samsung_leds_exit ( samsung );
	// samsung_rfkill_exit(samsung);
	// samsung_backlight_exit(samsung);
	samsung_sysfs_exit ( samsung );
	samsung_acpi_region_exit ( samsung );
	samsung_platform_exit ( samsung );
	kfree ( samsung );
	samsung_platform_device = NULL;
}

module_init ( samsung_init );
module_exit ( samsung_exit );

MODULE_AUTHOR ( "Pavel Sochelnikov <qwerty@4pda.ru>" );
MODULE_DESCRIPTION ( "Samsung AtivBook Keyboard LED driver" );
MODULE_LICENSE ( "GPL" );