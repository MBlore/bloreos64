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
#ifndef _BLOREOS_TERMINAL_BUFFER_H
#define _BLOREOS_TERMINAL_BUFFER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define TERM_MAX_LINES 1024
#define TERM_MAX_COLUMNS 256

typedef struct {
    uint32_t foreground;
    uint32_t background;
    char character;
} terminal_cell_t;

typedef struct {
    terminal_cell_t cells[TERM_MAX_COLUMNS];
    uint32_t length;
    bool continuation;
} terminal_line_t;

typedef struct {
    terminal_line_t lines[TERM_MAX_LINES];
    uint32_t head;
    uint32_t count;
} terminal_buffer_t;

void terminal_buffer_init(terminal_buffer_t *buffer);
void terminal_buffer_clear(terminal_buffer_t *buffer);

void terminal_buffer_write(terminal_buffer_t *buffer, const char *data, size_t length,
                           uint32_t foreground, uint32_t background);

const terminal_line_t *terminal_buffer_get_line(const terminal_buffer_t *buffer, uint32_t index);
uint32_t terminal_buffer_get_count(const terminal_buffer_t *buffer);

#endif