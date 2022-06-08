//
// Created by reui on 2022/5/25.
//

#ifndef COMMON_H
#define COMMON_H

enum BLE_OPCODE
{   
    BLE_INIT,
	BLE_BGM_CONN,
	BLE_GET_BGM_ADDR,
    BLE_STOP_SCAN,
};


extern int split (const char *str, char c, char ***arr);
void remove_substr (char *string, char *sub) ;
char *trim(char *str);

#endif //COMMON_H
