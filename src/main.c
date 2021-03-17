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
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lepton_task.h"
#include "send_task.h"
#include "wifi_utilities.h"
#include "system_config.h"
#include "system_utilities.h"


static const char* TAG = "main";


//
// Task handle externs for use by tasks to communicate with each other
//
TaskHandle_t task_handle_lepton;
TaskHandle_t task_handle_send;


void app_main()
{
    ESP_LOGI(TAG, "ESP32 startup");
        
    // Initialize the SPI and I2C drivers
    if (!lepton_io_init()) {
    	ESP_LOGE(TAG, "ESP32 init failed");
    	while (1) {vTaskDelay(pdMS_TO_TICKS(100));}
    }
    
    // Initialize Wifi connection
    if (!wifi_init()) {
    	ESP_LOGE(TAG, "WiFi initialization failed");
    	while (1) {vTaskDelay(pdMS_TO_TICKS(100));}
    }
    
    // Pre-allocate big buffers
    if (!lepton_buffer_init()) {
    	ESP_LOGE(TAG, "ESP32 memory allocate failed");
    	while (1) {vTaskDelay(pdMS_TO_TICKS(100));}
    }
    
    // Delay for Lepton internal initialization on power-on (max 950 mSec)
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Start tasks
    //  Core 0 : send task
    //  Core 1 : lepton task
    xTaskCreatePinnedToCore(&send_task, "send_task",  3072, NULL, 2, &task_handle_send,  0);
    xTaskCreatePinnedToCore(&lepton_task, "lepton_task",  2048, NULL, 19, &task_handle_lepton,  1);
}
