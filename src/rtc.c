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
#include <rtc.h>
#include <cpu.h>
#include <stddef.h>

#define CMOS_ADDRESS 0x70
#define CMOS_DATA    0x71

#define RTC_SECONDS  0x00
#define RTC_MINUTES  0x02
#define RTC_HOURS    0x04
#define RTC_DAY      0x07
#define RTC_MONTH    0x08
#define RTC_YEAR     0x09
#define RTC_STATUS_A 0x0A
#define RTC_STATUS_B 0x0B

#define RTC_MAX_ATTEMPTS 100000

/*
 * Reads a CMOS register with interrupts disabled by the caller.
 * The high bit of the address port disables NMIs temporarily.
 */
static uint8_t rtc_register(uint8_t index)
{
    outb(CMOS_ADDRESS, index | 0x80);
    uint8_t value = inb(CMOS_DATA);
    outb(CMOS_ADDRESS, 0x00);

    return value;
}

/*
 * Converts a packed BCD value to an ordinary integer.
 */
static uint8_t rtc_bcd_to_binary(uint8_t value)
{
    return (value & 0x0F) + ((value >> 4) * 10);
}

/*
 * Waits for the RTC update-in-progress flag to clear.
 */
static bool rtc_wait_ready(void)
{
    for (uint32_t i = 0; i < RTC_MAX_ATTEMPTS; i++) {
        if (!(rtc_register(RTC_STATUS_A) & 0x80)) {
            return true;
        }
    }

    return false;
}

/*
 * Reads all calendar fields and converts BCD and 12-hour
 * representations to binary 24-hour values.
 */
static bool rtc_snapshot(rtc_datetime_t *datetime)
{
    if (!rtc_wait_ready()) {
        return false;
    }

    uint8_t status = rtc_register(RTC_STATUS_B);
    uint8_t second = rtc_register(RTC_SECONDS);
    uint8_t minute = rtc_register(RTC_MINUTES);
    uint8_t hour = rtc_register(RTC_HOURS);
    uint8_t day = rtc_register(RTC_DAY);
    uint8_t month = rtc_register(RTC_MONTH);
    uint8_t year = rtc_register(RTC_YEAR);

    bool pm = (hour & 0x80) != 0;
    hour &= 0x7F;

    if (!(status & 0x04)) {
        second = rtc_bcd_to_binary(second);
        minute = rtc_bcd_to_binary(minute);
        hour = rtc_bcd_to_binary(hour);
        day = rtc_bcd_to_binary(day);
        month = rtc_bcd_to_binary(month);
        year = rtc_bcd_to_binary(year);
    }

    if (!(status & 0x02)) {
        hour = (hour % 12) + (pm ? 12 : 0);
    }

    datetime->year = 2000 + year;
    datetime->month = month;
    datetime->day = day;
    datetime->hour = hour;
    datetime->minute = minute;
    datetime->second = second;

    return true;
}

/*
 * Checks leap years using the Gregorian calendar.
 */
static bool rtc_leap_year(uint16_t year)
{
    return (year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0));
}

/*
 * Validates the decoded RTC date and time.
 */
static bool rtc_valid(const rtc_datetime_t *dt)
{
    static const uint8_t month_days[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };

    if (dt->year < 2000 || dt->year > 2099 || dt->month < 1 || dt->month > 12) {
        return false;
    }

    uint8_t days = month_days[dt->month - 1];

    if (dt->month == 2 && rtc_leap_year(dt->year)) {
        days++;
    }

    return dt->day >= 1 && dt->day <= days && dt->hour < 24 &&
           dt->minute < 60 && dt->second < 60;
}

/*
 * Reads the RTC twice until consecutive snapshots agree.
 * This avoids accepting a partially updated calendar value.
 *
 * Assumes the RTC has been configured to represent UTC.
 * The current driver assumes the years 2000 through 2099.
 */
bool rtc_read(rtc_datetime_t *datetime)
{
    if (datetime == NULL) {
        return false;
    }

    bool interrupts = set_interrupt_state(false);
    rtc_datetime_t first;
    rtc_datetime_t second;
    bool success = false;

    for (uint32_t attempt = 0; attempt < 10; attempt++) {
        if (!rtc_snapshot(&first) || !rtc_snapshot(&second)) {
            break;
        }

        if (first.year == second.year && first.month == second.month &&
            first.day == second.day && first.hour == second.hour &&
            first.minute == second.minute && first.second == second.second) {
            if (rtc_valid(&second)) {
                *datetime = second;
                success = true;
            }

            break;
        }
    }

    set_interrupt_state(interrupts);
    return success;
}