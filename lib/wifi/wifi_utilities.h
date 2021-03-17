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
#ifndef WIFI_UTILITIES_H
#define WIFI_UTILITIES_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_wifi.h"

//
// WiFi Utilities Constants
//

// Base part of the default SSID/Camera name - the last 4 nibbles of the ESP32's
// mac address are appended as ASCII characters
#define DEFAULT_AP_SSID "ESP32-Thermo-"

// Field lengths
#define SSID_MAX_LEN   32
#define PW_MAX_LEN     32


// wifi_info_t flags
#define WIFI_INFO_FLAG_STARTUP_ENABLE 0x01
#define WIFI_INFO_FLAG_INITIALIZED    0x02
#define WIFI_INFO_FLAG_ENABLED        0x04
#define WIFI_INFO_FLAG_CONNECTED      0x08
#define WIFI_INFO_FLAG_CL_STATIC_IP   0x10
#define WIFI_INFO_FLAG_CLIENT_MODE    0x80

// Maximum attempts to reconnect to an AP in client mode before starting to wait
#define WIFI_FAST_RECONNECT_ATTEMPTS   5

// Maximum number of AP stations to record when scanning
#define WIFI_MAX_SCAN_LIST_SIZE       10


//
// WiFi Utilities Data structures
//
typedef struct {
	char* ap_ssid;             // AP SSID is also the Camera Name
	char* sta_ssid;
	char* ap_pw;
	char* sta_pw;
	uint8_t flags;
	uint8_t ap_ip_addr[4];
	uint8_t sta_ip_addr[4];
	uint8_t sta_netmask[4];
	uint8_t cur_ip_addr[4];
} wifi_info_t;


//
// WiFi Utilities API
//
bool wifi_init();
bool wifi_is_connected();
bool wifi_scan_is_complete();
wifi_info_t* wifi_get_info();

#endif /* WIFI_UTILITIES_H */