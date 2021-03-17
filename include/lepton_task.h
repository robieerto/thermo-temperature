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
#ifndef LEP_TASK_H
#define LEP_TASK_H

#include <stdint.h>
#include <stdbool.h>



//
// LEP Task Constants
//

// Number of consecutive VoSPI resynchronization attempts before attempting to reset
#define LEP_SYNC_FAIL_FAULT_LIMIT 10

// Reset fail delay before attempting a re-init (seconds)
#define LEP_RESET_FAIL_RETRY_SECS 60



//
// LEP Task API
//
void lepton_task();
bool lepton_io_init();
bool lepton_buffer_init();


#endif /* LEP_TASK_H */