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
#ifndef _BLOREOS_STR_H
#define _BLOREOS_STR_H

#include <stddef.h>
#include <stdarg.h>

void reverse(char str[], size_t length);
size_t itoa(int num, char str[], size_t base);

int snprintf(char buffer[], size_t size, const char format[], ...);
int vsnprintf(char buffer[], size_t size, const char format[], va_list args);

#endif