/* ***************************************************************************
 * Copyright(C) 2021 Robert Mysza
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 * ***************************************************************************
 */
#ifndef RSP_TASK_H
#define RSP_TASK_H

#include <stdint.h>


//
// RSP Task Constants
//

// Task evaluation interval
#define RSP_TASK_SLEEP_MSEC 20

// Response Task notifications
#define RSP_NOTIFY_LEP_FRAME_MASK_0    0x00000010
#define RSP_NOTIFY_LEP_FRAME_MASK_1    0x00000020


#define WEB_SERVER "192.168.4.2"
#define HTTP_PORT 3000
#define SOCKET_PORT 8043
#define WEB_URL "/"

#define MAX_HTTP_RECV_BUFFER 512
#define MAX_HTTP_OUTPUT_BUFFER 2048

typedef enum protocol {
    TCP_FLAG,
    UDP_FLAG
} t_protocol;

//
// RSP Task API
//
void send_task();

#endif /* RSP_TASK_H */