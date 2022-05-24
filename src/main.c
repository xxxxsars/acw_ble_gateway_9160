/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr.h>
#include <stdio.h>
#include <string.h>
// #include <drivers/gps.h>
// #include <drivers/sensor.h>
#include <console/console.h>
// #include <net/nrf_cloud.h>
#include <dk_buttons_and_leds.h>
#include <modem/lte_lc.h>
#include <sys/reboot.h>
#include <modem/nrf_modem_lib.h>


#include "ble.h"
#include "lte.h"
#include "common.h"



#define BUTTON_1 BIT(0)
#define BUTTON_2 BIT(1)
#define SWITCH_1 BIT(2)
#define SWITCH_2 BIT(3)

#define LED_ON(x)			(x)
#define LED_BLINK(x)		((x) << 8)
#define LED_GET_ON(x)		((x) & 0xFF)
#define LED_GET_BLINK(x)	(((x) >> 8) & 0xFF)


enum {
	LEDS_INITIALIZING       = LED_ON(0),
	LEDS_LTE_CONNECTING     = LED_BLINK(DK_LED3_MSK),
	LEDS_LTE_CONNECTED      = LED_ON(DK_LED3_MSK),
	LEDS_CLOUD_CONNECTING   = LED_BLINK(DK_LED4_MSK),
	LEDS_CLOUD_PAIRING_WAIT = LED_BLINK(DK_LED3_MSK | DK_LED4_MSK),
	LEDS_CLOUD_CONNECTED    = LED_ON(DK_LED4_MSK),
	LEDS_ERROR              = LED_ON(DK_ALL_LEDS_MSK)
} display_state;

/**@brief Callback for button events from the DK buttons and LEDs library. */
static void button_handler(uint32_t buttons, uint32_t has_changed)
{
	printk("button_handler: button 1: %u, button 2: %u "
	       "switch 1: %u, switch 2: %u\n",
	       (bool)(buttons & BUTTON_1), (bool)(buttons & BUTTON_2),
	       (bool)(buttons & SWITCH_1), (bool)(buttons & SWITCH_2));
	

	if((bool)(buttons & SWITCH_1)){
		ble_action(BLE_GET_BGM_ADDR);
	}else if (!(bool)(buttons & SWITCH_1)){
		ble_action(BLE_STOP_SCAN);
	}
}


static void modem_configure(void)
{
	display_state = LEDS_LTE_CONNECTING;

	if (IS_ENABLED(CONFIG_LTE_AUTO_INIT_AND_CONNECT)) {
		/* Do nothing, modem is already turned on and connected. */
	} else {
		int err;

		printk("Establishing LTE link (this may take some time) ...\n");
		err = lte_lc_init_and_connect();
		__ASSERT(err == 0, "LTE link could not be established.");
		display_state = LEDS_LTE_CONNECTED;
	}
}

void main(void)
{
	printk("LTE Sensor Gateway sample started\n");

	

	int err = dk_buttons_init(button_handler);
	if (err) {
		printk("Could not initialize buttons, err code: %d\n", err);
	}

	ble_init();

	// modem_configure();
	// lte_get();
}