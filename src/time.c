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
#include <stddef.h>
#include <stdbool.h>
#include <time.h>
#include <hpet.h>
#include <rtc.h>

#define SECONDS_PER_DAY 86400ULL
#define UNIX_2000_EPOCH 946684800ULL

static uint64_t clock_epoch_seconds;
static uint64_t clock_boot_ms;
static bool clock_initialized;

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

/*
 * Returns whether a year is a Gregorian leap year.
 */
static bool time_leap_year(uint16_t year)
{
    return (year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0));
}

/*
 * Returns the number of days in a calendar month.
 */
static uint8_t time_days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t days[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };

    if (month == 2 && time_leap_year(year)) {
        return 29;
    }

    return days[month - 1];
}

/*
 * Converts a calendar date into Unix seconds.
 */
static uint64_t time_to_epoch(const rtc_datetime_t *dt)
{
    uint64_t days = 0;

    for (uint16_t year = 2000; year < dt->year; year++) {
        days += time_leap_year(year) ? 366 : 365;
    }

    for (uint8_t month = 1; month < dt->month; month++) {
        days += time_days_in_month(dt->year, month);
    }

    days += dt->day - 1;

    return UNIX_2000_EPOCH + days * SECONDS_PER_DAY +
           dt->hour * 3600ULL + dt->minute * 60ULL + dt->second;
}

/*
 * Converts Unix seconds back to a UTC calendar date.
 */
static void time_from_epoch(uint64_t epoch, rtc_datetime_t *dt)
{
    uint64_t days = epoch / SECONDS_PER_DAY;
    uint64_t remainder = epoch % SECONDS_PER_DAY;

    dt->hour = remainder / 3600;
    dt->minute = (remainder % 3600) / 60;
    dt->second = remainder % 60;

    uint16_t year = 1970;

    while (true) {
        uint16_t year_days = time_leap_year(year) ? 366 : 365;

        if (days < year_days) {
            break;
        }

        days -= year_days;
        year++;
    }

    uint8_t month = 1;

    while (true) {
        uint8_t month_days = time_days_in_month(year, month);

        if (days < month_days) {
            break;
        }

        days -= month_days;
        month++;
    }

    dt->year = year;
    dt->month = month;
    dt->day = days + 1;
}

/*
 * Initialises the wall clock from the RTC.
 *
 * Records the HPET uptime immediately after the RTC read.
 * Subsequent calls calculate elapsed time using HPET.
 */
bool time_init(void)
{
    rtc_datetime_t datetime;

    clock_initialized = false;

    if (!rtc_read(&datetime)) {
        return false;
    }

    clock_epoch_seconds = time_to_epoch(&datetime);
    clock_boot_ms = time_get_uptime_ms();
    clock_initialized = true;

    return true;
}

/*
 * Returns the system wall-clock time in Unix seconds.
 */
uint64_t time_get_unix_seconds(void)
{
    if (!clock_initialized) {
        return 0;
    }

    uint64_t elapsed_ms = time_get_uptime_ms() - clock_boot_ms;

    return clock_epoch_seconds + elapsed_ms / 1000;
}

/*
 * Returns the current UTC date and time.
 */
bool time_get_datetime(rtc_datetime_t *datetime)
{
    if (datetime == NULL || !clock_initialized) {
        return false;
    }

    time_from_epoch(time_get_unix_seconds(), datetime);
    return true;
}