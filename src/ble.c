/* main.c - Application main entry point */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/types.h>
#include <stdio.h>
#include <stddef.h>
#include <errno.h>

#include <zephyr.h>
#include <sys/printk.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/conn.h>
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>

#include <sys/byteorder.h>

#include "lib/common.h"

#define SCAN_INTERVAL 0x0140 /* 200 ms */
#define SCAN_WINDOW 0x0030	 /* 30 ms */
#define INIT_INTERVAL 0x0010 /* 10 ms */
#define INIT_WINDOW 0x0010	 /* 10 ms */
#define CONN_INTERVAL 0x00A0 /* 200 ms */
#define CONN_LATENCY 0

#define CONN_TIMEOUT MIN(MAX((CONN_INTERVAL * 125 *               \
							  MAX(CONFIG_BT_MAX_CONN, 6) / 1000), \
							 10),                                 \
						 3200)

// BGM uuid
#define BT_UUID_BGM_SERVICE BT_UUID_DECLARE_16(0xfee0)
#define BT_UUID_BGM_PCL BT_UUID_DECLARE_16(0xfee1)
#define BT_UUID_BGM_NOTIFY BT_UUID_DECLARE_16(0xfee2)
#define BT_UUID_BGM_WRITE BT_UUID_DECLARE_16(0xfee3)

// BGM handle
#define BT_HANDLE_BGM_PCL 45
#define BT_HANDLE_BGM_NOTIFY 48
#define BT_HANDLE_BGM_WRITE 52

// BGM Response code
#define BGM_FW_VERSION 0x01	   // 0xfe
#define BGM_ONE_RECODE 0x02	   // 0x9e
#define BGM_EIGHT_RECORD 0x08  // 0x9d
#define BGM_SERIAl_NUMBER 0x0b // 0x37

#define MAX_EIGHT_DATA 132
#define RESP_SIZE 1024
#define SN_SIZE 13

#define POST_DATA "{\"serial_number\": \"%s\",\"device_address\": \"%s\",\"data_index\": \"%u\",\"blood_glucose\": \"%u\",\"marker\": \"%s\",\"data_time\": \"%u-%u-%uT%u:%u%s\"}"


enum BGM_Marker
{
	BEFORE_MEAL,
	AFTER_MEAL,
	NO_MEAL,
	MOON_MEAL,
	BEDTIME_MEAL,
	SPORT_MEAL,
	WAKEUP_MEAL,
};

enum BGM_RESP_IDX
{
	RESP_CODE = 0,
	RESP_DATA_IDX = 1,
	IND_L = 4,
	IND_H,
	DA_0,
	DA_1,
	DA_2,
	DA_3,
	DA_4,
	DA_5,
};

static void start_scan();

static uint8_t volatile conn_count;
static uint8_t volatile conn_index;
static bool volatile is_disconnecting;
static bool  is_discovering;

static int volatile default_opcode;

static char ble_resp[RESP_SIZE];
static char sn[SN_SIZE];

static struct bt_uuid_16 uuid;
char devname[64];
struct bt_gatt_read_params conn_param;
static struct bt_conn *connections[CONFIG_BT_MAX_CONN];
static struct bt_gatt_discover_params discover_params;
static struct bt_gatt_subscribe_params subscribe_params[CONFIG_BT_MAX_CONN];

uint8_t eight_records[MAX_EIGHT_DATA];
int top = -1;
bool get_eight_records = false;

void print_resp_bytes(const void *pt, const uint8_t length, uint16_t data_length)
{
	union
	{
		uint8_t data8;
		uint16_t data16;
		uint32_t data32;
	} u32;
	const unsigned char *u8ptr = pt;

	for (uint8_t k = 0; k < data_length; k++)
	{
		switch (length)
		{
		case 16:
			memcpy(&u32.data16, u8ptr, sizeof(u32.data16));
			printk("0x%04" PRIx16 " ", u32.data16);
			u8ptr += sizeof(u32.data16);
			break;
		case 32:
			memcpy(&u32.data32, u8ptr, sizeof(u32.data32));
			printk("0x%08" PRIx32 " ", u32.data32);
			u8ptr += sizeof(u32.data32);
			break;
		default:
			printk("0x%02" PRIx8 " ", *u8ptr);
			u8ptr++;
			break;
		}
	}

	printk("\n\n");
}

void bgm_marker_to_str(const uint16_t marker, char *str, size_t len)
{

	switch (marker)
	{
	case BEFORE_MEAL:
		snprintk(str, len, "%s", "Befoare Meal");
		break;
	case AFTER_MEAL:
		snprintk(str, len, "%s", "After Meal");
		break;
	case NO_MEAL:
		snprintk(str, len, "%s", "No Meal");
		break;
	case MOON_MEAL:
		snprintk(str, len, "%s", "Moon Meal");
		break;
	case BEDTIME_MEAL:
		snprintk(str, len, "%s", "BedTime Meal");
		break;
	case SPORT_MEAL:
		snprintk(str, len, "%s", "Sport Meal");
		break;
	case WAKEUP_MEAL:
		snprintk(str, len, "%s", "Wakeup Meal");
		break;
	default:
		(void)memset(str, 0, len);
		return;
	}
}

void bgm_timezon_to_str(const uint16_t timezone, char *str, size_t len)
{

	if (timezone > 0xC)
	{
		snprintk(str, len, "-%02u:00", (timezone - 0xC));
	}
	else
	{
		snprintk(str, len, "+%02u:00", (0xC - timezone));
	}
}
void push(uint8_t *arr, uint8_t data)
{
	if (top >= MAX_EIGHT_DATA)
	{
		printk("Can't push to array.\n");
	}
	else
	{
		top++;
		arr[top] = data;
	}
}

uint8_t read_cb(struct bt_conn *conn, uint8_t err,
				struct bt_gatt_read_params *params,
				const void *data, uint16_t length)
{

	printk("in read cb\n");
	if (err)
	{

		printk("read err %u", err);
		// return 0;
	}

	memset(devname, 0, 64);
	memcpy(devname, data, length);
	return 0;
}

void write_cb(struct bt_conn *conn, uint8_t err,
			  struct bt_gatt_write_params *params)
{
	if (err)
	{
		printk("Write request completed with BT_ATT_ERR 0x%02x\n", err);
		printk("Write request completed with BT_ATT_ERR %d\n", err);
	}
	else
	{
		printk("Write Sucessfull handle %u data length:%d\n", params->handle, params->length);
	}
}

static uint8_t notify_func(struct bt_conn *conn,
						   struct bt_gatt_subscribe_params *params,
						   const void *data, uint16_t length)
{
	if (!data)
	{
		printk("[UNSUBSCRIBED]\n");
		params->value_handle = 0U;

		return BT_GATT_ITER_STOP;
	}

	char addr[BT_ADDR_LE_STR_LEN];
	bt_addr_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("[NOTIFICATION] %s\n", addr);

	
	if (data)
	{
		printk("Length :%d\n", length);

		// convert data to uint8 arrays
		uint8_t recv_data[length];
		memcpy(recv_data, data, length);

		// print_resp_bytes(data,8,length);
		switch (recv_data[RESP_CODE])
		{

		case BGM_ONE_RECODE:
		{
			uint8_t record_idx = (recv_data[IND_H] << 8) + recv_data[IND_L];
			printk("Record_idx : 0x%02x\n", record_idx);

			// parser fisrt response data
			if ((record_idx > 0) & (recv_data[RESP_DATA_IDX] == 1))
			{

				uint16_t year = (recv_data[DA_3] & 0x7F) + 0x7D0;
				uint16_t month = ((recv_data[DA_1] & 0xC0) >> 4) + ((recv_data[DA_0] & 0xC0) >> 6) + 1;
				uint16_t day = (recv_data[DA_0] & 0x1F) + 1;
				uint16_t hour = recv_data[DA_1] & 0x1F;
				uint16_t minute = recv_data[DA_2] & 0x3F;
				uint16_t glucose = ((recv_data[DA_4] & 0x03) << 8) + recv_data[DA_5];
				uint16_t timezone = ((recv_data[DA_1] & 0x20) >> 5) + ((recv_data[DA_2] & 0xC0) >> 5) + ((recv_data[DA_4] & 0xC0) >> 3);
				uint16_t marker = ((recv_data[DA_4] & 0x38) >> 3);
				char tz[10];
				bgm_timezon_to_str(timezone, tz, sizeof(tz));

				char marker_str[20];
				bgm_marker_to_str(marker, marker_str, sizeof(marker_str));
				// printk("%s Glucose:%u (%u/%u/%u %u:%u GMT %s)\n", marker_str, glucose, year, month, day, hour, minute, tz);

				snprintk(ble_resp, RESP_SIZE, POST_DATA, sn, addr, record_idx, glucose, marker_str, year, month, day, hour, minute, tz);
				printk("%s\n", ble_resp);

				
			}
			else if (recv_data[RESP_DATA_IDX] == 1)
			{
				uint16_t total_amount = (recv_data[DA_1] << 8) + recv_data[DA_0];
				uint16_t last_transfer = (recv_data[DA_5] << 8) + recv_data[DA_4];
				uint16_t max_amount = (recv_data[DA_3] << 8) + recv_data[DA_2];
				printk("Total Amount : 0x%03x\n", total_amount);
				printk("Last Transfer : 0x%03x\n", last_transfer);
				printk("Max Amount : %u\n", max_amount);
			}
			int err;

			// err = bt_gatt_unsubscribe(connections[conn_index], &subscribe_params[conn_index]);
			// if (err)
			// {
			// 	printk("unsunscribe error %d\n", err);
			// }

			// err = bt_conn_disconnect(connections[conn_index], BT_HCI_ERR_REMOTE_USER_TERM_CONN);
			// if (err) {

			// 	char addr[BT_ADDR_LE_STR_LEN];
			// 	int err;

			// 	bt_addr_le_to_str(bt_conn_get_dst(connections[conn_index]), addr, sizeof(addr));

			// 	printk("Failed disconnection %s.\n", addr);
			// }

			break;
		}
		// entry it will merge bytes array
		case BGM_EIGHT_RECORD:
		{
			int idx = 0;

			if (recv_data[RESP_DATA_IDX] == 7)
			{
				get_eight_records = true;
			}
			if (recv_data[RESP_DATA_IDX] != 1)
			{
				idx = 2;
			}

			// for(idx;idx < length;idx++) {
			// 	push(&eight_records, resp[idx]);
			// }

			while (idx < length)
			{
				push(&eight_records, recv_data[idx]);
				idx++;
			}

			break;
		}
		case BGM_SERIAl_NUMBER:
		{	
			memcpy(sn, data, length);
			sn[length -1] = '\0';    
			trim(sn);
			printk("global sn:%s\n",sn);
			break;
		}

		}

		// print eight records
		if (get_eight_records)
		{
			for (int i = 0; i < 8; i++)
			{
				int add_idx = 16 * i;
				uint16_t year = (eight_records[DA_3 + add_idx] & 0x7F) + 0x7D0;
				uint16_t month = ((eight_records[DA_1 + add_idx] & 0xC0) >> 4) + ((eight_records[DA_0 + add_idx] & 0xC0) >> 6) + 1;
				uint16_t day = (eight_records[DA_0 + add_idx] & 0x1F) + 1;
				uint16_t hour = eight_records[DA_1 + add_idx] & 0x1F;
				uint16_t minute = eight_records[DA_2 + add_idx] & 0x3F;
				uint16_t glucose = ((eight_records[DA_4 + add_idx] & 0x03) << 8) + eight_records[DA_5 + add_idx];
				uint16_t timezone = ((eight_records[DA_1 + add_idx] & 0x20) >> 5) + ((eight_records[DA_2 + add_idx] & 0xC0) >> 5) + ((eight_records[DA_4 + add_idx] & 0xC0) >> 3);
				uint16_t marker = ((eight_records[DA_4 + add_idx] & 0x38) >> 3);

				char tz[10];
				bgm_timezon_to_str(timezone, tz, sizeof(tz));

				char dest[20];
				bgm_marker_to_str(marker, dest, sizeof(dest));
				printk("%s Glucose:%u (%u/%u/%u %u:%u GMT %s)\n", dest, glucose, year, month, day, hour, minute, tz);
			}

			get_eight_records = false;
			top = -1;
			memset(eight_records, 0, MAX_EIGHT_DATA);
		}

		// int err = bt_gatt_unsubscribe(connections[conn_index], &subscribe_params[conn_index]);
		// if (err){
		// 	printk("unsunscribe error %d\n",err);
		// }
	}

	return BT_GATT_ITER_CONTINUE;
}

static uint8_t discover_func(struct bt_conn *conn,
							 const struct bt_gatt_attr *attr,
							 struct bt_gatt_discover_params *params)
{
	int err;

	if (!attr)
	{
		printk("Discover complete\n");
		(void)memset(params, 0, sizeof(*params));
		return BT_GATT_ITER_STOP;
	}

	char addr[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("[ATTRIBUTE] handle %u %s %u\n", attr->handle, addr, conn_index);

	if (!bt_uuid_cmp(discover_params.uuid, BT_UUID_BGM_SERVICE))
	{
		memcpy(&uuid, BT_UUID_BGM_NOTIFY, sizeof(uuid));
		discover_params.uuid = &uuid.uuid;
		discover_params.start_handle = attr->handle + 1;
		discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

		err = bt_gatt_discover(connections[conn_index], &discover_params);
		if (err)
		{
			printk("Discover failed (err %d)\n", err);
		}
	}
	else if (!bt_uuid_cmp(discover_params.uuid,
						  BT_UUID_BGM_NOTIFY))
	{
		memcpy(&uuid, BT_UUID_GATT_CCC, sizeof(uuid));
		discover_params.uuid = &uuid.uuid;
		discover_params.start_handle = attr->handle + 2;
		discover_params.type = BT_GATT_DISCOVER_DESCRIPTOR;
		subscribe_params[conn_index].value_handle = bt_gatt_attr_value_handle(attr);

		err = bt_gatt_discover(connections[conn_index], &discover_params);
		if (err)
		{
			printk("Discover failed (err %d)\n", err);
		}
	}
	else
	{
		is_discovering = false;
		subscribe_params[conn_index].notify = notify_func;
		subscribe_params[conn_index].value = BT_GATT_CCC_NOTIFY;
		subscribe_params[conn_index].ccc_handle = attr->handle;

		err = bt_gatt_subscribe(connections[conn_index], &subscribe_params[conn_index]);
		if (err && err != -EALREADY)
		{
			printk("Subscribe failed (err %d)\n", err);
		}
		else
		{
			printk("[SUBSCRIBED]\n");

			// Read One Record Type 1(read amount of blood)
			static uint8_t type1_data[] = {0xB0, 0x61, 0x01, 0x00, 0x12};
			static struct bt_gatt_write_params write_op;
			write_op = (struct bt_gatt_write_params){
				.handle = BT_HANDLE_BGM_WRITE,
				.offset = 0,
				.data = &type1_data,
				.length = sizeof(type1_data),
				.func = write_cb,
			};
			err = bt_gatt_write(connections[conn_index], &write_op);
			if (err)
			{
				printk("Write request failed (%d)\n", err);
			}

			// //Read One Record Type2 ( read specific blood glucose)
			// static uint8_t type2_data[] = { 0xB0,0x61,0x07,0x00,0x18 };

			// write_op.data = &type2_data;
			// write_op.length = sizeof(type2_data);

			// err = bt_gatt_write(conn, &write_op);
			// if (err) {
			// 	printk("Write request failed (%d)\n", err);
			// }

			// 	//Read Eight Record
			// 	static uint8_t eight_recode_data[] = { 0xB0,0x62,0x08,0x00,0x1A };

			// 	write_op.data = &eight_recode_data;
			// 	write_op.length = sizeof(eight_recode_data);

			// 	err = bt_gatt_write(conn, &write_op);
			// 	if (err) {
			// 		printk("Write request failed (%d)\n", err);
			// 	}
		}

		return BT_GATT_ITER_STOP;
	}
}

static bool eir_found(struct bt_data *data, void *user_data)
{
	bt_addr_le_t *addr = user_data;
	int i;

	struct bt_conn_le_create_param create_param = {
		.options = BT_CONN_LE_OPT_NONE,
		.interval = INIT_INTERVAL,
		.window = INIT_WINDOW,
		.interval_coded = 0,
		.window_coded = 0,
		.timeout = 0,
	};

	struct bt_le_conn_param conn_param = {
		.interval_min = CONN_INTERVAL,
		.interval_max = CONN_INTERVAL,
		.latency = CONN_LATENCY,
		.timeout = CONN_TIMEOUT,
	};

	// char addr_str[BT_ADDR_LE_STR_LEN];
	// int err;
	// bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
	// printk("Device found: %s \n", addr_str);
	// printk("[AD]: %u data_len %u\n", data->type, data->data_len);

	switch (data->type)
	{
	case BT_DATA_UUID16_SOME:
	case BT_DATA_UUID16_ALL:
		if (data->data_len % sizeof(uint16_t) != 0U)
		{
			printk("AD malformed\n");
			return true;
		}

		for (i = 0; i < data->data_len; i += sizeof(uint16_t))
		{
			struct bt_le_conn_param *param;
			struct bt_uuid *uuid;
			uint16_t u16;
			int err;

			memcpy(&u16, &data->data[i], sizeof(u16));
			uuid = BT_UUID_DECLARE_16(sys_le16_to_cpu(u16));
			if (bt_uuid_cmp(uuid, BT_UUID_BGM_SERVICE))
			{
				continue;
			}

			err = bt_le_scan_stop();
			if (err)
			{
				printk("Stop LE scan failed (err %d)\n", err);
				continue;
			}

			if (strlen(ble_resp) > 0)
			{

				printk("had data: %s  opcode:%d\n", ble_resp, default_opcode);

			}

			switch (default_opcode)
			{
			case BLE_GET_BGM_ADDR:
			{
				char dest[BT_ADDR_LE_STR_LEN];
				bt_addr_le_to_str(addr, dest, sizeof(dest));
				printk("scaned %s\n", dest);

				start_scan();
				break;
			}
			case BLE_STOP_SCAN:
			{
				break;
			}

			case BLE_BGM_CONN:
			{
				struct bt_conn *temp_conn;
				err = bt_conn_le_create(addr, &create_param, &conn_param,
										&temp_conn);

				if (err)
				{
					char dest[BT_ADDR_LE_STR_LEN];
					bt_addr_le_to_str(addr, dest, sizeof(dest));
					printk("Create conn failed (err %d) address: %s\n", err, dest);
					start_scan();
				}
				else
				{
					conn_index = bt_conn_index(temp_conn);
					char addr[BT_ADDR_LE_STR_LEN];
					bt_addr_le_to_str(bt_conn_get_dst(temp_conn), addr, sizeof(addr));

					printk("temp conn index:%u %s\n", conn_index, addr);
					connections[conn_index] = temp_conn;
					temp_conn = NULL;
					return false;
				}

				break;
			}

			default:
			{
				start_scan();
				break;
			}
			}

			return false;
		}
	}

	return true;
}

static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
						 struct net_buf_simple *ad)
{

	// printk all ble device
	//  char dev[BT_ADDR_LE_STR_LEN];
	//  bt_addr_le_to_str(addr, dev, sizeof(dev));
	//  printk("[DEVICE]: %s, AD evt type %u, AD data len %u, RSSI %i\n",
	//         dev, type, ad->len, rssi);

	if (type == BT_GAP_ADV_TYPE_ADV_IND ||
		type == BT_GAP_ADV_TYPE_ADV_DIRECT_IND)
	{

		bt_data_parse(ad, eir_found, (void *)addr);
		/* We're only interested in connectable events */
	}
	else if (type != BT_GAP_ADV_TYPE_ADV_IND &&
			 type != BT_GAP_ADV_TYPE_ADV_DIRECT_IND)
	{
		return;
	}
}

static void start_scan()
{
	int err;

	/* Use active scanning and disable duplicate filtering to handle any
	 * devices that might update their advertising data at runtime. */
	struct bt_le_scan_param scan_param = {
		.type = BT_LE_SCAN_TYPE_ACTIVE,
		.options = BT_LE_SCAN_OPT_NONE,
		.interval = BT_GAP_SCAN_FAST_INTERVAL,
		.window = BT_GAP_SCAN_FAST_WINDOW,
	};

	err = bt_le_scan_start(&scan_param, device_found);
	if (err)
	{
		printk("Scanning failed to start (err %d)\n", err);
		return;
	}

	printk("Scanning successfully started\n");
}

static void connected(struct bt_conn *conn, uint8_t conn_err)
{
	char addr[BT_ADDR_LE_STR_LEN];
	int err;

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (conn_err)
	{
		printk("Failed to connect to %s (%u)\n", addr, conn_err);

		bt_conn_unref(connections[conn_index]);
		connections[conn_index] = NULL;

		start_scan();
		return;
	}

	for (int i = 0; i < CONFIG_BT_MAX_CONN; i++)
	{
		if (connections[i])
		{

			char addr[BT_ADDR_LE_STR_LEN];
			bt_addr_le_to_str(bt_conn_get_dst(connections[i]), addr, sizeof(addr));
			printk("Connections: %d, %s\n", i, addr);
		}
		else
		{
			printk("Connections %d empty.\n", i);
		}
	}

	if (conn == connections[conn_index])
	{
		printk("Initial discover function (%u).\n", conn_index);

		static uint8_t data[] = {0x00};
		static struct bt_gatt_write_params write_op;
		write_op = (struct bt_gatt_write_params){
			.handle = BT_HANDLE_BGM_PCL,
			.offset = 0,
			.data = &data,
			.length = sizeof(data),
			.func = write_cb,
		};
		err = bt_gatt_write(connections[conn_index], &write_op);
		if (err)
		{
			printk("Write request failed (%d)\n", err);
		}

		// if (!is_discovering)
		// {
		// 	printk("in discovering.\n");
			is_discovering = true;

			memcpy(&uuid, BT_UUID_BGM_SERVICE, sizeof(uuid));
			discover_params.uuid = &uuid.uuid;
			discover_params.func = discover_func;
			discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
			discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
			discover_params.type = BT_GATT_DISCOVER_PRIMARY;

			err = bt_gatt_discover(conn, &discover_params);
			if (err)
			{
				printk("Discover failed(err %d)\n", err);
				return;
			}
		// }
	}

	conn_count++;

	if (conn_count < CONFIG_BT_MAX_CONN)
	{
		start_scan();
	}

	printk("Connected (%u): %s\n", conn_count, addr);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Disconnected: %s (reason 0x%02x)\n", addr, reason);

	connections[conn_index] = NULL;
	bt_conn_unref(conn);

	if ((conn_count == 1U) && is_disconnecting)
	{
		is_disconnecting = false;
		start_scan();
	}
	conn_count--;
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing cancelled: %s\n", addr);
}

static void pairing_confirm(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	bt_conn_auth_pairing_confirm(conn);

	printk("Pairing confirmed: %s\n", addr);
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing completed: %s, bonded: %d\n", addr, bonded);
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing failed conn: %s, reason %d\n", addr,
		   reason);
}

static struct bt_conn_auth_cb conn_auth_callbacks = {
	.cancel = auth_cancel,
	.pairing_confirm = pairing_confirm,
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed};

static void bt_ready(int err)
{
	// k_busy_wait(10 * 1000); // DB hash delayed work is 10 MS
	if (IS_ENABLED(CONFIG_SETTINGS))
	{
		settings_load();
	}

	err = bt_conn_auth_cb_register(&conn_auth_callbacks);
	if (err)
	{
		printk("Failed to register authorization callbacks.\n");
		return;
	}

	start_scan();
}

static void disconnect(struct bt_conn *conn, void *data)
{
	char addr[BT_ADDR_LE_STR_LEN];
	int err;

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Disconnecting %s...\n", addr);
	err = bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	if (err)
	{
		printk("Failed disconnection %s.\n", addr);
	}
	printk("success.\n");
}

void ble_action(int opcode)
{

	// if last action was stop scan ,it will rescan ble devices.
	if (default_opcode == BLE_STOP_SCAN)
	{
		start_scan();
	}

	default_opcode = opcode;
}

void get_ble_resp(char *resp, int buffersize)
{

	if (!resp || buffersize < 1)
		return;
	if (!strlen(ble_resp))
	{
		*resp = '\0'; // Return an 'empty' string
	}
	else
	{
		size_t length = strlen(ble_resp);
		strncpy(resp, ble_resp, length);

		resp[length - 1] = '\0';
	}
}

void ble_init(void)
{

	int err;

	err = bt_enable(bt_ready);

	if (err)
	{
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}

	printk("Bluetooth initialized\n");



}
