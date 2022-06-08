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
		ble_action(BLE_BGM_CONN);
	}else if (!(bool)(buttons & SWITCH_1)){
		ble_action(BLE_STOP_SCAN);
	}

}

// static K_KERNEL_STACK_DEFINE(my_tcp_rx_thread_stack, 2048);
// static struct k_thread my_tcp_rx_thread_data;

void test(){
	char resp[1024];
	while (true)
	{
		 get_ble_resp(resp,1024);

		 if (strlen(resp)>1){
			 break;
		 }
	}
	printk("get ble_resp\n");
	printk("%s\n",resp);
	char *post_data = "{\"serial_number\": \"123123\",\"device_address\": \"C2:05:09:C1:C3:00\",\"data_index\": \"1\",\"blood_glucose\": \"149\",\"marker\": \"No Meal\",\"data_time\": \"2018-1-4T8:42+08:00\"}";
	
	post_blood_glucose(post_data);
	
}


void main(void)
{
	printk("LTE Sensor Gateway sample started\n");

	

	int err = dk_buttons_init(button_handler);
	if (err) {
		printk("Could not initialize buttons, err code: %d\n", err);
	}



	// printk("Waiting for network..\n");

	// modem_configure();

	// printk("OK\n");


	ble_init();
	


	// k_thread_create(&my_tcp_rx_thread_data, my_tcp_rx_thread_stack,
	// 		K_KERNEL_STACK_SIZEOF(my_tcp_rx_thread_stack),
	// 		(k_thread_entry_t)test, NULL, NULL, NULL,
	// 		K_PRIO_PREEMPT(CONFIG_MAIN_THREAD_PRIORITY), 0, K_NO_WAIT);


}