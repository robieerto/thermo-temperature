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
#include "wifi_utilities.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_event_loop.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include <lwip/sockets.h>
#include <string.h>



//
// Wifi Utilities local variables
//
static const char* TAG = "wifi_utilities";

// Wifi information
static char wifi_ap_ssid_array[SSID_MAX_LEN+1];
static char wifi_sta_ssid_array[SSID_MAX_LEN+1];
static char wifi_ap_pw_array[PW_MAX_LEN+1];
static char wifi_sta_pw_array[PW_MAX_LEN+1];
static wifi_info_t wifi_info = {
	wifi_ap_ssid_array,
	wifi_sta_ssid_array,
	wifi_ap_pw_array,
	wifi_sta_pw_array,
	0,
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0}
};

static bool sta_connected = false; // Set when we connect to an AP so we can disconnect if we restart
static int sta_retry_num = 0;

static bool scan_in_progress = false;
static bool got_scan_done_event = false; // Set when an AP Scan is complete

// FreeRTOS event group to signal when we are connected
static EventGroupHandle_t wifi_event_group;



//
// WiFi Utilities Forward Declarations for internal functions
//
static void set_wifi_info();
static bool init_esp_wifi();
static bool enable_esp_wifi_ap();
static bool enable_esp_wifi_client();
static esp_err_t sys_event_handler(void *ctx, system_event_t* event);
static char nibble_to_ascii(uint8_t n);

//
// WiFi Utilities API
//

/**
 * Power-on initialization of the WiFi system.  It is enabled based on start-up
 * information from persistent storage.  Returns false if any part of the initialization
 * fails.
 */
bool wifi_init()
{
	esp_err_t ret;
	
	// Setup the event handler
	wifi_event_group = xEventGroupCreate();
	ret = esp_event_loop_init(sys_event_handler, NULL);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Could not initialize event loop handler (%d)", ret);
		return false;
	}

	//Initialize NVS
    ret = nvs_flash_init();
	if ((ret == ESP_ERR_NVS_NO_FREE_PAGES) || (ret == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
		// NVS partition was truncated and needs to be erased
		ret = nvs_flash_erase();
		if (ret != ESP_OK) {
			ESP_LOGE(TAG, "NVS Erase failed with err %d", ret);
			return false;
		}
		
		// Retry init
		ret = nvs_flash_init();
		if (ret != ESP_OK) {
			ESP_LOGE(TAG, "NVS Init failed with err %d", ret);
			return false;
		}
	}
	
	// Initialize the TCP/IP stack
	tcpip_adapter_init();
	
	// Get our wifi info
	set_wifi_info();
	
	// Initialize the WiFi interface
	if (init_esp_wifi()) {
		wifi_info.flags |= WIFI_INFO_FLAG_INITIALIZED;
		
		// Configure the WiFi interface if enabled
		if ((wifi_info.flags & WIFI_INFO_FLAG_STARTUP_ENABLE) != 0) {
			if ((wifi_info.flags & WIFI_INFO_FLAG_CLIENT_MODE) != 0) {
				if (enable_esp_wifi_client()) {
					wifi_info.flags |= WIFI_INFO_FLAG_ENABLED;
					ESP_LOGI(TAG, "WiFi Station starting");
				} else {
					return false;
				}
			} else {
				if (enable_esp_wifi_ap()) {
					wifi_info.flags |= WIFI_INFO_FLAG_ENABLED;
					ESP_LOGI(TAG, "WiFi AP %s enabled", wifi_info.ap_ssid);
				} else {
					return false;
				}
			}
		}
	} else {
		return false;
	}
	
	return true;
}


/**
 * Return connected to client status
 */
bool wifi_is_connected()
{
	return ((wifi_info.flags & WIFI_INFO_FLAG_CONNECTED) != 0);
}


/**
 * Return scan completion status
 */
bool wifi_scan_is_complete()
{
	return got_scan_done_event;
}


/**
 * Return current WiFi configuration and state
 */
wifi_info_t* wifi_get_info()
{
	return &wifi_info;
}


//
// WiFi Utilities internal functions
//
static void set_wifi_info()
{
	int i;
	uint8_t sys_mac_addr[6];
	
	// Wifi parameters
	//
	// Get the system's default MAC address and add 1 to match the "Soft AP" mode
	// (see "Miscellaneous System APIs" in the ESP-IDF documentation)
	esp_efuse_mac_get_default(sys_mac_addr);
	sys_mac_addr[5] = sys_mac_addr[5] + 1;
	
	// Construct our default AP SSID/Camera name
	for (i=0; i<SSID_MAX_LEN+1; i++) {
		wifi_info.ap_ssid[i] = 0;
		wifi_info.ap_pw[i] = 0;
		wifi_info.sta_ssid[i] = 0;
		wifi_info.sta_pw[i] = 0;
	}
	sprintf(wifi_info.ap_ssid, "%s%c%c%c%c", DEFAULT_AP_SSID,
		    nibble_to_ascii(sys_mac_addr[4] >> 4),
		    nibble_to_ascii(sys_mac_addr[4]),
		    nibble_to_ascii(sys_mac_addr[5] >> 4),
	 	    nibble_to_ascii(sys_mac_addr[5]));
	
	wifi_info.flags = WIFI_INFO_FLAG_STARTUP_ENABLE;
	
	wifi_info.ap_ip_addr[3] = 192;
	wifi_info.ap_ip_addr[2] = 168;
	wifi_info.ap_ip_addr[1] = 4;
	wifi_info.ap_ip_addr[0] = 1;
	wifi_info.sta_ip_addr[3] = 192;
	wifi_info.sta_ip_addr[2] = 168;
	wifi_info.sta_ip_addr[1] = 4;
	wifi_info.sta_ip_addr[0] = 2;
	wifi_info.sta_netmask[3] = 255;
	wifi_info.sta_netmask[2] = 255;
	wifi_info.sta_netmask[1] = 255;
	wifi_info.sta_netmask[0] = 0;
}


/**
 * Initialize the WiFi interface resources
 */
static bool init_esp_wifi()
{
	esp_err_t ret;
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	
	ret = esp_wifi_init(&cfg);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Could not allocate wifi resources (%d)", ret);
		return false;
	}

	ret = esp_wifi_set_storage(WIFI_STORAGE_RAM);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Could not set RAM storage for configuration (%d)", ret);
		return false;
	}
	
	return true;
}


/**
 * Enable this device as a Soft AP
 */
static bool enable_esp_wifi_ap()
{
	esp_err_t ret;
	int i;
	
	// Enable the AP
	wifi_config_t wifi_config = {
        .ap = {
            .ssid_len = strlen(wifi_info.ap_ssid),
            .max_connection = 1,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        }
    };
    strcpy((char*) wifi_config.ap.ssid, wifi_info.ap_ssid);
    strcpy((char*) wifi_config.ap.password, wifi_info.ap_pw);
    if (strlen(wifi_info.ap_pw) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }
    
    ret = esp_wifi_set_mode(WIFI_MODE_AP);
    if (ret != ESP_OK) {
    	ESP_LOGE(TAG, "Could not set Soft AP mode (%d)", ret);
    	return false;
    }
    
    ret = esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config);
    if (ret != ESP_OK) {
    	ESP_LOGE(TAG, "Could not set Soft AP configuration (%d)", ret);
    	return false;
    }
    
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
    	ESP_LOGE(TAG, "Could not start Soft AP (%d)", ret);
    	return false;
    }
    
    // For now, since we are using the default IP address, copy it to the current here
    for (i=0; i<4; i++) {
    	wifi_info.cur_ip_addr[i] = wifi_info.ap_ip_addr[i];
    }
    	
    return true;
}


/**
 * Enable this device as a Client
 */
static bool enable_esp_wifi_client()
{
	esp_err_t ret;
	tcpip_adapter_ip_info_t ipInfo;
	
	// Configure the IP address mechanism
	if ((wifi_info.flags & WIFI_INFO_FLAG_CL_STATIC_IP) != 0) {
		// Static IP
		ret = tcpip_adapter_dhcpc_stop(TCPIP_ADAPTER_IF_STA);
		if (ret != ESP_OK) {
    		ESP_LOGW(TAG, "Stop Station DHCP returned %d", ret);
    	}
    	
		ipInfo.ip.addr = wifi_info.sta_ip_addr[3] |
						 (wifi_info.sta_ip_addr[2] << 8) |
						 (wifi_info.sta_ip_addr[1] << 16) |
						 (wifi_info.sta_ip_addr[0] << 24);
  		inet_pton(AF_INET, "0.0.0.0", &ipInfo.gw);
  		ipInfo.netmask.addr = wifi_info.sta_netmask[3] |
						     (wifi_info.sta_netmask[2] << 8) |
						     (wifi_info.sta_netmask[1] << 16) |
						     (wifi_info.sta_netmask[0] << 24);
  		tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_STA, &ipInfo);
	} else {
		ret = tcpip_adapter_dhcpc_start(TCPIP_ADAPTER_IF_STA);
		if (ret != ESP_OK) {
    		ESP_LOGE(TAG, "Start Station DHCP returned %d", ret);
    	}
	}
	
	// Enable the Client
	wifi_config_t wifi_config = {
		.sta = {
			.scan_method = WIFI_FAST_SCAN,
			.bssid_set = 0,
			.channel = 0,
			.listen_interval = 0,
			.sort_method = WIFI_CONNECT_AP_BY_SIGNAL			
		}
	};	
    strcpy((char*) wifi_config.sta.ssid, wifi_info.sta_ssid);
    if (strlen(wifi_info.sta_pw) == 0) {
        strcpy((char*) wifi_config.sta.password, "");
    } else {
    	strcpy((char*) wifi_config.sta.password, wifi_info.sta_pw);
    }
    
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
    	ESP_LOGE(TAG, "Could not set Station mode (%d)", ret);
    	return false;
    }
    
    ret = esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
    if (ret != ESP_OK) {
    	ESP_LOGE(TAG, "Could not set Station configuration (%d)", ret);
    	return false;
    }
    
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
    	ESP_LOGE(TAG, "Could not start Station (%d)", ret);
    	return false;
    }
    
    return true;
}


/**
 * Handle system events that we care about from the WiFi task
 */
static esp_err_t sys_event_handler(void *ctx, system_event_t* event)
{
	switch(event->event_id) {
		case SYSTEM_EVENT_AP_STACONNECTED:
			wifi_info.flags |= WIFI_INFO_FLAG_CONNECTED;
			ESP_LOGI(TAG, "station:"MACSTR" join, AID=%d",
                 MAC2STR(event->event_info.sta_connected.mac),
                 event->event_info.sta_connected.aid);
			break;
		
		case SYSTEM_EVENT_AP_STADISCONNECTED:
			wifi_info.flags &= ~WIFI_INFO_FLAG_CONNECTED;
			ESP_LOGI(TAG, "station:"MACSTR" leave, AID=%d",
                 MAC2STR(event->event_info.sta_disconnected.mac),
                 event->event_info.sta_disconnected.aid);
			break;
			
		case SYSTEM_EVENT_STA_START:
			if (scan_in_progress) {
				ESP_LOGI(TAG, "Station started for scan");
			} else {
				ESP_LOGI(TAG, "Station started, trying to connect to %s", wifi_info.sta_ssid);
				esp_wifi_connect();
			}
			sta_retry_num = 0;
        	break;
        
        case SYSTEM_EVENT_STA_STOP:
        	ESP_LOGI(TAG, "Station stopped");
        	break;
        	
        case SYSTEM_EVENT_STA_GOT_IP:
        	wifi_info.flags |= WIFI_INFO_FLAG_CONNECTED;
        	uint32_t ip = event->event_info.got_ip.ip_info.ip.addr;
        	ESP_LOGI(TAG, "Connected. Got ip: %s", ip4addr_ntoa((ip4_addr_t *)&event->event_info.got_ip.ip_info.ip));
        	wifi_info.cur_ip_addr[3] = ip & 0xFF;
        	wifi_info.cur_ip_addr[2] = (ip >> 8) & 0xFF;
        	wifi_info.cur_ip_addr[1] = (ip >> 16) & 0xFF;
			wifi_info.cur_ip_addr[0] = (ip >> 24) & 0xFF;
			sta_connected = true;
        	sta_retry_num = 0;
        	break;
        	
        case SYSTEM_EVENT_STA_DISCONNECTED:
        	wifi_info.flags &= ~WIFI_INFO_FLAG_CONNECTED;
        	if (sta_connected && !scan_in_progress) {
        		if (sta_retry_num > WIFI_FAST_RECONNECT_ATTEMPTS) {
        			vTaskDelay(pdMS_TO_TICKS(1000));
        		} else {
        			sta_retry_num++;
        		}
                esp_wifi_connect();
                ESP_LOGI(TAG, "Retry connection to %s", wifi_info.sta_ssid);
            }
        	break;
        
        case SYSTEM_EVENT_SCAN_DONE:
        	ESP_LOGI(TAG, "Scan done");
        	scan_in_progress = false;
        	got_scan_done_event = true;
        	break;
        
		default:
			break;
	}
	
	return ESP_OK;
}

/**
 * Return an ASCII character version of a 4-bit hexadecimal number
 */
static char nibble_to_ascii(uint8_t n)
{
	n = n & 0x0F;
	
	if (n < 10) {
		return '0' + n;
	} else {
		return 'A' + n - 10;
	}
}
