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
#ifndef SYSTEM_CONFIG_H
#define SYSTEM_CONFIG_H

#include "esp_system.h"

//
// IO Pins
//   Lepton uses HSPI (no MOSI)
//
#define BTN_IO            27
#define RED_LED_IO        32
#define GREEN_LED_IO      33

#define LEP_SCK_IO        18
#define LEP_CSN_IO        5
#define LEP_VSYNC_IO      4
#define LEP_MISO_IO       19
#define LEP_RESET_IO      23

#define I2C_MASTER_SDA_IO 21
#define I2C_MASTER_SCL_IO 22

//
// Hardware Configuration
//

// I2C
#define I2C_MASTER_NUM     1
#define I2C_MASTER_FREQ_HZ 100000

// SPI
//   Lepton uses HSPI (no MOSI)
//   LCD and TS use VSPI
#define LEP_SPI_HOST    HSPI_HOST
#define LEP_DMA_NUM     2
#define LEP_SPI_FREQ_HZ 16000000


#endif // SYSTEM_CONFIG_H
