/*
    BloreOS - Operating System
    Copyright (C) 2023 Martin Blore

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#include <crc32.h>

#define CRC32_POLYNOMIAL    0xEDB88320U
#define CRC32_INITIAL_VALUE 0xFFFFFFFFU
#define CRC32_FINAL_XOR     0xFFFFFFFFU
#define CRC32_BITS_PER_BYTE 8

// Calculate the standard IEEE CRC32 checksum.
uint32_t crc32(const void *data, size_t size)
{
    const uint8_t *bytes = data;
    uint32_t crc = CRC32_INITIAL_VALUE;

    for (size_t i = 0; i < size; i++) {
        crc ^= bytes[i];

        for (uint32_t bit = 0; bit < CRC32_BITS_PER_BYTE; bit++) {
            if (crc & 1U) {
                crc = (crc >> 1) ^ CRC32_POLYNOMIAL;
            } else {
                crc >>= 1;
            }
        }
    }

    return crc ^ CRC32_FINAL_XOR;
}