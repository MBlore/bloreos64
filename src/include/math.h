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

#ifndef _BLOREOS_MATH_H
#define _BLOREOS_MATH_H

/* Divide and round up e.g. 5 / 4 = 1.25 = 2 */
#define DIV_ROUNDUP(val, div) (((val) + (div) - 1) / (div))

#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define MIN(a, b) ((a) < (b) ? (a) : (b))

/* Aligns val to the next highest multiple of align, e.g. val 10 and align 8 results in 16 */
#define ALIGN_UP(val, align) (((val) + (align) - 1) / (align)) * (align)

#endif
