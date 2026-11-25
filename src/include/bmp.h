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
#ifndef _BLOREOS_BITMAP_H
#define _BLOREOS_BITMAP_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    const uint8_t *pixels;
    uint32_t width;
    uint32_t height;
    uint64_t pitch;
    uint16_t bpp;
    bool top_down;
} bmp_t;

bool bmp_load(bmp_t *bitmap, const void *data, size_t size);
void bmp_draw(const bmp_t *bitmap, int64_t x, int64_t y);

#endif