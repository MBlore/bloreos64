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
#include <alloc.h>
#include <bump.h>
#include <slob.h>
#include <str.h>

#ifdef ALLOC_USESLOB
void *malloc(size_t size)
{
    return slob_malloc(size);
}

void free(void *ptr)
{
    slob_free(ptr);
}
#endif

#ifdef ALLOC_USEBUMP
void *malloc(size_t size)
{
    return bump_malloc(size);
}

void free(void *ptr)
{
    bump_free(ptr);
}
#endif

