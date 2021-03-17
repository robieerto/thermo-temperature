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
#include "lepton_task.h"
#include "send_task.h"
#include "system_utilities.h"
#include "system_config.h"
#include "esp_system.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_http_client.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include "vospi.h"


// Uncomment to log processing timestamps
//#define LOG_PROC_TIMESTAMP


//
// RSP Task variables
//
static const char* TAG = "send_task";

// State
static bool got_image_0, got_image_1;

// Connection socket
static int sockfd;

// Image buffer
static uint16_t send_img_buffer[LEP_NUM_PIXELS];

//
// RSP Task Forward Declarations for internal functions
//
static void handle_notifications();
static int process_image(int n);
static void send_response(uint16_t* rsp, int len);
static int socket_connect(t_protocol prot);
esp_err_t _http_event_handle(esp_http_client_event_t *evt);
static int http_get();

//
// RSP Task API
//
void send_task()
{
	int len;
	
	ESP_LOGI(TAG, "Start task");
	
	while (1) {
		// Process notifications from other tasks
		handle_notifications();
		
		// Look for things to send
		if (got_image_0 || got_image_1) {
			if (got_image_0) {
				len = process_image(0);
				got_image_0 = false;
			} else {
				len = process_image(1);
				got_image_1 = false;
			}	
				
			// Send the image
			if (len != 0) {
				send_response(send_img_buffer, LEP_NUM_PIXELS*2);
			}
		}
		
		// Sleep task
		vTaskDelay(pdMS_TO_TICKS(RSP_TASK_SLEEP_MSEC));
	} 
}

//
// Internal functions
//

/**
 * Handle incoming notifications
 */
static void handle_notifications()
{
	uint32_t notification_value;
	
	notification_value = 0;
	if (xTaskNotifyWait(0x00, 0xFFFFFFFF, &notification_value, 0)) {
		// Handle lepton_task notifications
		if (Notification(notification_value, RSP_NOTIFY_LEP_FRAME_MASK_0)) {
			got_image_0 = true;
		}
		if (Notification(notification_value, RSP_NOTIFY_LEP_FRAME_MASK_1)) {
			got_image_1 = true;
		}
	}
}


/**
 * Convert lepton data in the specified half of the ping-pong buffer into a json record
 * with delimitors for transmission over the network
 */
static int process_image(int n)
{
#ifdef LOG_PROC_TIMESTAMP
	int64_t tb, te;
	
	tb = esp_timer_get_time();
#endif
	
	// Convert the image into a json record
	xSemaphoreTake(lep_buffer[n].lep_mutex, portMAX_DELAY);
	void* send_img_buffer_ptr = &send_img_buffer;
	memcpy(send_img_buffer_ptr, lep_buffer[n].lep_bufferP, LEP_NUM_PIXELS*2);
	xSemaphoreGive(lep_buffer[n].lep_mutex);
	
#ifdef LOG_PROC_TIMESTAMP
	te = esp_timer_get_time();
	ESP_LOGI(TAG, "process_image took %d uSec", (int) (te - tb));
#endif

	return LEP_NUM_PIXELS*2;
}

/**
 * Send a response
 */
static void send_response(uint16_t* rsp, int rsp_length)
{
#ifdef LOG_SEND_TIMESTAMP
	int64_t tb, te;
	
	tb = esp_timer_get_time();
#endif
	
  socket_connect(TCP_FLAG);

  int err_code;
  int err_code_size = sizeof(err_code);
  getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &err_code, (socklen_t *)&err_code_size);

  if (err_code) {
    close(sockfd);
    return;
  }

  if (http_get(rsp_length)) {
    return;
  }

  if (send(sockfd, rsp, (size_t)rsp_length, 0) < 0) {
    ESP_LOGE(TAG, "Error connect: %s", strerror(errno));
    return;
  }

  close(sockfd);

  ESP_LOGE(TAG, "Image sent to server");

#ifdef LOG_SEND_TIMESTAMP
	te = esp_timer_get_time();
	ESP_LOGI(TAG, "send_response took %d uSec", (int) (te - tb));
#endif
}

/**
 * Connect to webserver through socket
 */
int socket_connect(t_protocol prot)
{
  // initializes address
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(SOCKET_PORT);
  if (!inet_aton(WEB_SERVER, &addr.sin_addr)) {
    ESP_LOGE(TAG, "Network address wrong format");
    return 1;
  }

  if (prot == TCP_FLAG) {
    // open TCP socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP)) < 0) {
      ESP_LOGE(TAG, "Creating of socket failed: %s", strerror(errno));
      return 1;
    }
  } else if (prot == UDP_FLAG) {
    // open UDP socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
      ESP_LOGE(TAG, "Creating of socket failed: %s", strerror(errno));
      return 1;
    }
  }

  // connect to server
  if ((connect(sockfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in))) != 0) {
    ESP_LOGE(TAG, "Cannot establish the connection");
    return 1;
  }

  ESP_LOGE(TAG, "Successfully connected to server");

  return 0;
}

/**
 * Handle HTTP state events
 */
esp_err_t _http_event_handle(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER");
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (!esp_http_client_is_chunked_response(evt->client)) {
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
    }
    return ESP_OK;
}

/**
 * Send HTTP GET query
 */
int http_get()
{
    // char local_response_buffer[MAX_HTTP_OUTPUT_BUFFER] = { 0 };

    esp_http_client_config_t config = {
        .host = WEB_SERVER,
        .port = HTTP_PORT,
        .path = "/",
        .query = "camera=flir",
        .event_handler = _http_event_handle,
        // .user_data = local_response_buffer,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
	
    // GET
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }
    // ESP_LOG_BUFFER_HEX(TAG, local_response_buffer, strlen(local_response_buffer));
    esp_http_client_cleanup(client);

    return err;
}
