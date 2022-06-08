/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdio.h>
#include <string.h>
#include <zephyr.h>
#include <stdlib.h>
#include <net/socket.h>
#include <modem/nrf_modem_lib.h>
#include <modem/lte_lc.h>
#include <modem/modem_key_mgmt.h>


#include "lib/common.h"
#include "lib/yyjson.h"



#define BUF_SIZE 2048

#define HTTP_GET "GET"
#define HTTP_POST "POST"


#define HTTP_PORT "8000"
#define HTTP_HOSTNAME "reui.dsmynas.com"


#define HTTP_HEAD                                       \
    "HTTP/1.1\r\n"                                \
    "Host: " HTTP_HOSTNAME ":" HTTP_PORT  "\r\n"                  \
    "Content-Type: application/json; charset=utf-8\r\n" \
    "Connection: close\r\n"



void modem_configure(void)
{
	if (IS_ENABLED(CONFIG_LTE_AUTO_INIT_AND_CONNECT)) {
		/* Do nothing, modem is already turned on and connected. */
	} else {
		int err;
		printk("Establishing LTE link (this may take some time) ...\n");
		err = lte_lc_init_and_connect();
        // err = lte_lc_init_and_connect_async(NULL);
        
		__ASSERT(err == 0, "LTE link could not be established.");
	}
}


int tcp_socket_open(const char *host, uint16_t host_port)
{
    int err;
    int fd;


    struct addrinfo *res;
    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };


    err = getaddrinfo(host, NULL, &hints, &res);
    if (err)
    {
        printk("getaddrinfo() failed, err %d\n", errno);
        return -1;
    }

    ((struct sockaddr_in *)res->ai_addr)->sin_port = htons(host_port);

    fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == -1)
    {
        printk("Failed to open socket!\n");
        goto clean_up;
    }


    //setting recv and send timeout
    struct timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;

    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                   sizeof timeout) < 0)
    {
        printk("setsockopt failed\n");
    }

    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout,
                   sizeof timeout) < 0)
    {
        printk("setsockopt failed\n");
    }

    printk("Connecting to %s\n", HTTP_HOSTNAME);
    err = connect(fd, res->ai_addr, sizeof(struct sockaddr_in));
    if (err)
    {
        printk("connect() failed, err: %d\n", errno);
        goto clean_up;
    }

clean_up:
    freeaddrinfo(res);
    if (err) 
	{
		(void)close(fd);
		fd = -1;
	}

	return fd;
}




void post_blood_glucose(char * post_data)
{


    char *resp_status;
    int bytes;
    size_t off;

    char recv_buf[BUF_SIZE];
    char send_buf[BUF_SIZE];

    // char *post_data =  "{\"serial_number\":\"2772UAQ0214\",\"device_address\":\"B0:C2:05:09:C1:C3\",\"data_index\":\"1\",\"blood_sugar\":\"157\",\n"
    //                    "\"timezone\":\"GMT+8\"}";
    char *post_url = "/blood_glucose";
    sprintf(send_buf, HTTP_POST " %s " HTTP_HEAD "Content-Length: %d\r\n\r\n%s" ,post_url,strlen(post_data),post_data);

    // char *get_url = "/";
    // sprintf(send_buf, HTTP_GET " %s " HTTP_HEAD "\r\n",get_url);
    int head_len = strlen(send_buf);

    int fd = tcp_socket_open(HTTP_HOSTNAME,atoi(HTTP_PORT));

    off = 0;
    do
    {
        bytes = send(fd, &send_buf[off], head_len - off, 0);
        if (bytes < 0)
        {
            printk("send() failed, err %d\n", errno);
            (void)close(fd);

        }
        off += bytes;
    } while (off < head_len);

    printk("Sent %d bytes\n", off);

    off = 0;
    do
    {
        bytes = recv(fd, &recv_buf[off], BUF_SIZE - off, 0);
        if (bytes < 0)
        {
            printk("recv() failed, err %d\n", errno);
            (void)close(fd);
            break;
        }
        off += bytes;
    } while (bytes != 0 /* peer closed connection */);

    printk("Received %d bytes\n", off);


    char **arr = NULL;
    int content_length = 0;

    int lines = split(recv_buf, '\r\n', &arr);

    for (int i = 0; i < lines; i++)
    {    
        char *line = arr[i];

        //get body length
        char *substr = "Content-Length: ";
        char *match = strstr(line, substr);
       

        if (match) {
            remove_substr(line, substr);
            content_length = atoi(line);
        }


        // get response json body
        if (i == lines - 1 && content_length != 0)
        {
            line[content_length ] ='\0';
            printk("%s [%d]\n",line,strlen(line));

            // // Read JSON
            // yyjson_doc *doc = yyjson_read(line, strlen(line), 0);
            // yyjson_val *root = yyjson_doc_get_root(doc);

            // yyjson_val *message = yyjson_obj_get(root, "message");
            // printk("message: %s\n", yyjson_get_str(message));
            // printk("message length:%d\n", (int)yyjson_get_len(message));
        }
    }

    /* Print HTTP response */
    resp_status = strstr(recv_buf, "\r\n");

    if (resp_status)
    {
        off = resp_status - recv_buf;
        recv_buf[off + 1] = '\0';
        printk("\n>\t %s\n\n", recv_buf);
    }

    printk("Finished, closing socket.\n");


    // lte_lc_power_off();
}
