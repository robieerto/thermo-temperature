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
#include <stdbool.h>
#include "esp_system.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/spi_master.h"
#include "freertos/task.h"
#include "lepton_task.h"
#include "send_task.h"
#include "lepton_utilities.h"
#include "system_utilities.h"
#include "wifi_utilities.h"
#include "i2c.h"
#include "cci.h"
#include "vospi.h"
#include "system_config.h"


//
// LEP Task constants
//

// States
#define STATE_INIT      0
#define STATE_RUN       1
#define STATE_RE_INIT   2
#define STATE_ERROR     3


//
// LEP Task variables
//
static const char* TAG = "lepton_task";


//// Global buffer pointers for memory allocated in the external SPIRAM
// Shared memory data structures
lep_buffer_t lep_buffer[2];   // Ping-pong buffer loaded by lepton_task for send_task


//
// LEP Task API
//

/**
 * This task drives the Lepton camera interface.
 */
void lepton_task()
{
	int task_state = STATE_INIT;
	int rsp_buf_index = 0;
	int vsync_count = 0;
	int sync_fail_count = 0;
	int reset_fail_count = 0;
	int64_t vsyncDetectedUsec;
	
	ESP_LOGI(TAG, "Start task");

	// Initialize the control signals
	gpio_set_direction(LEP_VSYNC_IO, GPIO_MODE_INPUT);
	gpio_set_direction(LEP_RESET_IO, GPIO_MODE_OUTPUT);
	gpio_set_level(LEP_RESET_IO, 1);
	
	// Attempt to initialize the VoSPI interface
	if (vospi_init() != ESP_OK) {
		ESP_LOGE(TAG, "Lepton VoSPI initialization failed");
		vTaskDelete(NULL);
	}

	while (true) {
		switch (task_state) {
			case STATE_INIT:  // After power-on reset
				if (lepton_init()) {
					task_state = STATE_RUN;
				} else {
					ESP_LOGE(TAG, "Lepton CCI initialization failed");
					
					task_state = STATE_ERROR;
					// Use reset_fail_count as a timer
					reset_fail_count = LEP_RESET_FAIL_RETRY_SECS;
				}
				break;
			
			case STATE_RUN:   // Initialized and running
				// Spin waiting for vsync to be asserted
				while (gpio_get_level(LEP_VSYNC_IO) == 0) {
					vTaskDelay(pdMS_TO_TICKS(9));
				}
				vsyncDetectedUsec = esp_timer_get_time();
				
				// Attempt to process a segment
				if (vospi_transfer_segment(vsyncDetectedUsec)) {
					// Got image
					vsync_count = 0;
					
					// Copy the frame to the current half of the shared buffer and let send_task know
					xSemaphoreTake(lep_buffer[rsp_buf_index].lep_mutex, portMAX_DELAY);
					vospi_get_frame(&lep_buffer[rsp_buf_index]);
					xSemaphoreGive(lep_buffer[rsp_buf_index].lep_mutex);
#ifdef LOG_ACQ_TIMESTAMP
					ESP_LOGI(TAG, "Push into buf %d", rsp_buf_index);
#endif
					if (rsp_buf_index == 0) {
						xTaskNotify(task_handle_send, RSP_NOTIFY_LEP_FRAME_MASK_0, eSetBits);
						rsp_buf_index = 1;
					} else {
						xTaskNotify(task_handle_send, RSP_NOTIFY_LEP_FRAME_MASK_1, eSetBits);
						rsp_buf_index = 0;
					}
					
					// Hold fault counters reset while operating
					sync_fail_count = 0;
					reset_fail_count = 0;
				} else {
					// We should see a valid frame every 12 vsync interrupts (one frame period).
					// However, since we may be resynchronizing with the VoSPI stream and our task
					// may be interrupted by other tasks, we give the lepton extra frame periods
					// to start correctly streaming data.  We may still fail when the lepton runs
					// a FFC since that takes a long time.
					if (++vsync_count == 36) {
						vsync_count = 0;
						ESP_LOGI(TAG, "Could not get lepton image");
						
						// Pause to allow resynchronization
						// (Lepton 3.5 data sheet section 4.2.3.3.1 "Establishing/Re-Establishing Sync")
						vTaskDelay(pdMS_TO_TICKS(185));
						
						// Check for too many consecutive resynchronization failures.
						// This should only occur if something has gone wrong.
						if (sync_fail_count++ == LEP_SYNC_FAIL_FAULT_LIMIT) {
							if (reset_fail_count == 0) {
								// Reset the first time
								task_state = STATE_RE_INIT;
							} else {
								ESP_LOGE(TAG, "Could not sync to VoSPI after task reset");
								
								// Possibly permanent error condition
								task_state = STATE_ERROR;
								
								// Use reset_fail_count as a timer
								reset_fail_count = LEP_RESET_FAIL_RETRY_SECS;
							}
						}
					}
				}
				break;
			
			case STATE_RE_INIT:  // Reset and re-init
				ESP_LOGI(TAG,  "Reset Lepton");
				
				// Assert hardware reset
				gpio_set_level(LEP_RESET_IO, 0);
				vTaskDelay(pdMS_TO_TICKS(10));
				gpio_set_level(LEP_RESET_IO, 1);
				
				// Delay for Lepton internal initialization (max 950 mSec)
    			vTaskDelay(pdMS_TO_TICKS(1000));
    			
    			// Attempt to re-initialize the Lepton
    			if (lepton_init()) {
					task_state = STATE_RUN;
					
					// Note the reset
    				reset_fail_count = 1;
				} else {
					ESP_LOGE(TAG, "Lepton CCI initialization failed");
					
					task_state = STATE_ERROR;
					// Use reset_fail_count as a timer
					reset_fail_count = LEP_RESET_FAIL_RETRY_SECS;
				}
				break;
			
			case STATE_ERROR:  // Initialization or re-init failed
				// Do nothing for a good long while
				vTaskDelay(pdMS_TO_TICKS(1000));
				if (--reset_fail_count == 0) {
					// Attempt another reset/re-init
					task_state =  STATE_RE_INIT;
				}
				break;
			
			default:
				task_state = STATE_RE_INIT;
		}
	}
}


/**
 * Initialize the ESP32 GPIO and internal peripherals
 */
bool lepton_io_init()
{
	ESP_LOGI(TAG, "ESP32 Peripheral Initialization");	
	
	// Attempt to initialize the I2C Master
	if (i2c_master_init() != ESP_OK) {
		ESP_LOGE(TAG, "I2C Master initialization failed");
		return false;
	}
	
	// Attempt to initialize the SPI Master used by the Lepton
	spi_bus_config_t spi_buscfg1 = {
		.miso_io_num=LEP_MISO_IO,
		.mosi_io_num=-1,
		.sclk_io_num=LEP_SCK_IO,
		.max_transfer_sz=LEP_PKT_LENGTH,
		.quadwp_io_num=-1,
		.quadhd_io_num=-1
	};
	if (spi_bus_initialize(LEP_SPI_HOST, &spi_buscfg1, LEP_DMA_NUM) != ESP_OK) {
		ESP_LOGE(TAG, "Lepton Master initialization failed");
		return false;
	}
	
	return true;
}


/**
 * Allocate shared buffers for use by tasks for image data in the external RAM
 */
bool lepton_buffer_init()
{
	ESP_LOGI(TAG, "Buffer Allocation");
	
	// Allocate the LEP/RSP task lepton frame and telemetry ping-pong buffers
	lep_buffer[0].lep_bufferP = heap_caps_malloc(LEP_NUM_PIXELS*2, MALLOC_CAP_DMA);
	if (lep_buffer[0].lep_bufferP == NULL) {
		ESP_LOGE(TAG, "malloc RSP lepton shared image buffer 0 failed");
		return false;
	}
	lep_buffer[0].lep_telemP = heap_caps_malloc(LEP_TEL_WORDS*2, MALLOC_CAP_DMA);
	if (lep_buffer[0].lep_telemP == NULL) {
		ESP_LOGE(TAG, "malloc RSP lepton shared telemetry buffer 0 failed");
		return false;
	}
	lep_buffer[1].lep_bufferP = heap_caps_malloc(LEP_NUM_PIXELS*2, MALLOC_CAP_DMA);
	if (lep_buffer[1].lep_bufferP == NULL) {
		ESP_LOGE(TAG, "malloc RSP lepton shared image buffer 1 failed");
		return false;
	}
	lep_buffer[1].lep_telemP = heap_caps_malloc(LEP_TEL_WORDS*2, MALLOC_CAP_DMA);
	if (lep_buffer[1].lep_telemP == NULL) {
		ESP_LOGE(TAG, "malloc RSP lepton shared telemetry buffer 1 failed");
		return false;
	}
	
	// Create the ping-pong buffer access mutexes
	lep_buffer[0].lep_mutex = xSemaphoreCreateMutex();
	if (lep_buffer[0].lep_mutex == NULL) {
		ESP_LOGE(TAG, "create RSP lepton mutex 0 failed");
		return false;
	}
	lep_buffer[1].lep_mutex = xSemaphoreCreateMutex();
	if (lep_buffer[1].lep_mutex == NULL) {
		ESP_LOGE(TAG, "create RSP lepton mutex 1 failed");
		return false;
	}
	
	return true;
}
