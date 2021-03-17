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
#ifndef SYSTEM_UTILITIES_H
#define SYSTEM_UTILITIES_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "system_config.h"
#include <stdbool.h>
#include <stdint.h>


//
// System Utilities typedefs
//

//
// System Utilities macros
//
#define Notification(var, mask) ((var & mask) == mask)


// Buffer typedef
typedef struct {
	bool telem_valid;
	uint16_t lep_min_val;
	uint16_t lep_max_val;
	uint16_t* lep_bufferP;
	uint16_t* lep_telemP;
	SemaphoreHandle_t lep_mutex;
} lep_buffer_t;


//
// Task handle externs for use by tasks to communicate with each other
//
extern TaskHandle_t task_handle_lepton;
extern TaskHandle_t task_handle_send;

//
// Global buffer pointers for allocated memory
//

// Shared memory data structures
extern lep_buffer_t lep_buffer[2];   // Ping-pong buffer loaded by lepton_task for send_task


#endif /* SYSTEM_UTILITIES_H */