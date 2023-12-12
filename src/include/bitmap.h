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

---------------------------------------------------------------------------------

    Bitmap functions for storing a large amount of true/false flags.

    For example, if you have 2 bytes in binary form:
    
    00000000 00000000

    If you use bitmap_on and use bit value 10, it will set the second bytes 2nd bit:

    00000000 01000000

    It's very useful for indicating what pages in the memory map are taken.
*/
#ifndef _BLOREOS_BITMAP_H
#define _BLOREOS_BITMAP_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* Returns true if the bit at the specified location starting at the param 'bitmap' is set to 1 */
static inline bool bitmap_test(const void *bitmap, size_t bit) {
    const uint8_t *bitmap_u8 = (const uint8_t*)bitmap;

    size_t byte_index = bit / 8;
    uint8_t bit_mask = 1 << (bit % 8);

    return (bitmap_u8[byte_index] & bit_mask) != 0;
}

/* Sets the bit at the specified location starting at the param 'bitmap' to 1 */
static inline void bitmap_on(void *bitmap, size_t bit) {
    uint8_t *bitmap_u8 = (uint8_t*)bitmap;

    bitmap_u8[bit / 8] |= (1 << (bit % 8));
}

/* Clears the bit at the specified location starting at the param 'bitmap' to 0 */
static inline void bitmap_off(void *bitmap, size_t bit) {
    uint8_t *bitmap_u8 = (uint8_t*)bitmap;

    bitmap_u8[bit / 8] &= ~(1 << (bit % 8));
}


#endif