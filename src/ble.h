/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _BLE_H_
#define _BLE_H_

void ble_action(int opcode);
void ble_init(void);
void get_ble_resp(char* resp, int buffersize);

#endif /* _BLE_H_ */
