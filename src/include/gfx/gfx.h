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
#ifndef _BLOREOS_GFX_H
#define _BLOREOS_GFX_H

#include <stdint.h>

void gfx_init(void);

uint32_t gfx_get_width(void);
uint32_t gfx_get_height(void);
uint32_t gfx_get_glyph_width(void);
uint32_t gfx_get_glyph_height(void);

void gfx_put_pixel(uint32_t x, uint32_t y, uint32_t color);
void gfx_clear(uint32_t color);
void gfx_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);
void gfx_draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);

void gfx_draw_glyph(char ch, uint32_t x, uint32_t y, uint32_t foreground, uint32_t background);
void gfx_draw_text(uint32_t x, uint32_t y, const char *text, uint32_t foreground, uint32_t background);

#endif