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
#ifndef _BLOREOS_TERMINAL_H
#define _BLOREOS_TERMINAL_H

#include <stdint.h>
#include <ps2.h>

#define TERM_DEFAULT_FGCOLOR 0xCCCCCC
#define TERM_CURSOR_COLOR 0xFFFFFF

void term_init();
void tprintf(const char format[], ...);
void term_fgcolor(uint32_t color);
void term_bgcolor(uint32_t color);
void term_keyevent(KeyEvent_t *ke);
void term_cblink();
void term_clear();

#endif