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
#include <stdint.h>
#include <time.h>
#include <hpet.h>

/*
 * Return the monotonic kernel uptime in milliseconds.
 */
uint64_t time_get_uptime_ms(void)
{
    return hpet_get_uptime_ms();
}

/*
 * Sleep for the specified number of milliseconds.
 *
 * Currently implemented using HPET counter polling
 * until a kernel scheduler is available.
 */
void time_sleep_ms(uint64_t ms)
{
    if (ms == 0)
        return;

    hpet_sleep(ms);
}