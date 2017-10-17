#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/scx200_gpio.h>
#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/efi.h>
#include <linux/dmi.h>

#define ATIVBOOK_KB_LED_DRVNAME "ativbook_kbd_led"

#define ATIVBOOK_ACPI_REGION_NAME "\\SNVS"
#define ATIVBOOK_ACPI_MEM_TARGET 0xCA1CDF18
#define ATIVBOOK_ACPI_MEM_LENGTH 0x00D0

#define ATIVBOOK_ACPI_OFF_KBTO 0x7C
#define ATIVBOOK_ACPI_OFF_KBST 0x8E
#define ATIVBOOK_ACPI_OFF_KBLL 0x8F

#define ATIVBOOK_ACPI_FN_UPDATE "\\SECS"
#define ATIVBOOK_ACPI_UPDATE_ON 0x9A
#define ATIVBOOK_ACPI_UPDATE_OFF 0x9A

// #define ATIVBOOK_ACPI_FN_AUTO "\\_SB.PCI0.LPCB.H_EC._Q70"

static unsigned char * acpi_memory = NULL;
static acpi_handle acpi_fn_update = NULL;
// static acpi_handle acpi_fn_auto = NULL;

static void ativbook_kb_led_set ( struct led_classdev * led_cdev, enum led_brightness value )
{
	acpi_status status;
	union acpi_object param[1];
	struct acpi_object_list input;

	if ( value > 4 )
		value = 4;

	if ( 0 > value )
		value = 0;

	* ( acpi_memory + ATIVBOOK_ACPI_OFF_KBLL ) = value;

	param[0].type = ACPI_TYPE_INTEGER;
	param[0].integer.value = ( value > 0 ? ATIVBOOK_ACPI_UPDATE_ON : ATIVBOOK_ACPI_UPDATE_OFF );
	input.count = 1;
	input.pointer = param;

	status = acpi_evaluate_object ( acpi_fn_update, NULL, &input, NULL );

	if ( ACPI_FAILURE ( status ) )
		pr_err ( "AtivBook acpi update keyboard backlight fail!\n" );
}

static enum led_brightness ativbook_kb_led_get ( struct led_classdev * led_cdev )
{
	enum led_brightness value;

	value = * ( acpi_memory + ATIVBOOK_ACPI_OFF_KBLL );

	return value;
}

static struct led_classdev ativbook_kb_led = {
	.name			= "ativbook::kbd_backlight",
	.brightness_set	= ativbook_kb_led_set,
	.brightness_get = ativbook_kb_led_get,
	.max_brightness = 4,
	.flags			= LED_CORE_SUSPENDRESUME,
};

static int ativbook_kb_led_probe ( struct platform_device * pdev )
{
	int ret;

	acpi_handle handle;
	acpi_status status;

	status = acpi_get_handle ( NULL, ATIVBOOK_ACPI_REGION_NAME, &handle );

	if ( ACPI_FAILURE ( status ) ) {
		return -ENODEV;
	}

	// I don't know how get address and length region from handle, use predefine parameters

	status = acpi_get_handle ( NULL, ATIVBOOK_ACPI_FN_UPDATE, &acpi_fn_update );

	if ( ACPI_FAILURE ( status ) ) {
		return -ENODEV;
	}

	/*
	status = acpi_get_handle ( NULL, ATIVBOOK_ACPI_FN_AUTO, &acpi_fn_auto );

	if ( ACPI_FAILURE ( status ) ) {
		return -ENODEV;
	}
	*/

	acpi_memory = acpi_os_map_memory ( ATIVBOOK_ACPI_MEM_TARGET, ATIVBOOK_ACPI_MEM_LENGTH );

	if ( !acpi_memory ) {
		return -ENOMEM;
	}

	ret = led_classdev_register ( &pdev->dev, &ativbook_kb_led );

	if ( ret ) {
		acpi_os_unmap_memory ( acpi_memory, ATIVBOOK_ACPI_MEM_LENGTH );
	}

	return ret;
}

static int ativbook_kb_led_remove ( struct platform_device * pdev )
{
	acpi_os_unmap_memory ( acpi_memory, ATIVBOOK_ACPI_MEM_LENGTH );
	led_classdev_unregister ( &ativbook_kb_led );
	return 0;
}

static struct platform_driver ativbook_kb_led_driver = {
	.probe = ativbook_kb_led_probe,
	.remove = ativbook_kb_led_remove,
	.driver = {
		.name = ATIVBOOK_KB_LED_DRVNAME,
	},
};

static struct dmi_system_id __initconst ativbook_dmi_table[] = {
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
MODULE_DEVICE_TABLE ( dmi, ativbook_dmi_table );

static struct platform_device * ativbook_platform_device;

static int __init samsung_init ( void )
{
	int ret;

	if ( !efi_enabled ( EFI_BOOT ) ) {
		pr_err ( "!EFI_BOOT" );
		return -ENODEV;
	}

	if ( !dmi_check_system ( ativbook_dmi_table ) ) {
		pr_err ( "!dmi_check_system" );
		return -ENODEV;
	}

	ret = platform_driver_register ( &ativbook_kb_led_driver );

	if ( ret < 0 )
		goto out;

	ativbook_platform_device = platform_device_register_simple ( ATIVBOOK_KB_LED_DRVNAME, -1, NULL, 0 );

	if ( IS_ERR ( ativbook_platform_device ) ) {
		ret = PTR_ERR ( ativbook_platform_device );
		platform_driver_unregister ( &ativbook_kb_led_driver );
		goto out;
	}


out:
	return ret;

}

static void __exit samsung_exit ( void )
{
	platform_device_unregister ( ativbook_platform_device );
	platform_driver_unregister ( &ativbook_kb_led_driver );
}

module_init ( samsung_init );
module_exit ( samsung_exit );

MODULE_AUTHOR ( "Pavel Sochelnikov <qwerty@4pda.ru>" );
MODULE_DESCRIPTION ( "Samsung AtivBook Keyboard LED driver" );
MODULE_LICENSE ( "GPL" );