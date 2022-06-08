/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr.h>
#include <stdio.h>
#include <string.h>
#include <regex.h>


// #include <drivers/gps.h>
// #include <drivers/sensor.h>
#include <console/console.h>
// #include <net/nrf_cloud.h>
#include <dk_buttons_and_leds.h>
#include <modem/lte_lc.h>
#include <sys/reboot.h>
#include <modem/nrf_modem_lib.h>


// #include "lte.h"
#include "ble.h"
#include "lib/common.h"



#define BUTTON_1 BIT(0)
#define BUTTON_2 BIT(1)
#define SWITCH_1 BIT(2)
#define SWITCH_2 BIT(3)



/**@brief Callback for button events from the DK buttons and LEDs library. */
static void button_handler(uint32_t buttons, uint32_t has_changed)
{
	printk("button_handler: button 1: %u, button 2: %u "
	       "switch 1: %u, switch 2: %u\n",
	       (bool)(buttons & BUTTON_1), (bool)(buttons & BUTTON_2),
	       (bool)(buttons & SWITCH_1), (bool)(buttons & SWITCH_2));
	

	if((bool)(buttons & SWITCH_1)){
		ble_action(BLE_BGM_CONN);
	}else if (!(bool)(buttons & SWITCH_1)){
		ble_action(BLE_STOP_SCAN);
	}

}


void main(void)
{
	printk("Initial ble testing.....\n");

	
	int err = dk_buttons_init(button_handler);
	if (err) {
		printk("Could not initialize buttons, err code: %d\n", err);
	}

	ble_init();
	


}