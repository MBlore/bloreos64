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
#ifndef _BLOREOS_PS2_H
#define _BLOREOS_PS2_H

#include <stdint.h>
#include <stdbool.h>

#define PS2_KEYDOWN             0
#define PS2_KEYUP               1
#define PS2_SCANCODE_BACKSPACE  14
#define PS2_SCANCODE_ENTER      28

typedef struct KeyEvent {
    uint8_t scan_code;
    uint8_t event_type;     // 0 = key down, 1 = key up
    char ascii;
    bool is_control;
} KeyEvent_t;

extern KeyEvent_t* scancode_map[];

uint8_t ps2_read();
uint8_t ps2_read_no_wait();
void ps2_init();

#endif