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

#include <fat32.h>
#include <time.h>
#include <rtc.h>
#include <stddef.h>

/*
 * Encodes a calendar date into the FAT32 packed date format.
 *
 * Bits 15-9: years since 1980
 * Bits 8-5: month
 * Bits 4-0: day
 */
static uint16_t fat32_pack_date(const rtc_datetime_t *dt)
{
    return ((uint16_t)(dt->year - 1980) << 9) |
           ((uint16_t)dt->month << 5) | dt->day;
}

/*
 * Encodes a time into the FAT32 packed time format.
 *
 * Bits 15-11: hours
 * Bits 10-5: minutes
 * Bits 4-0: seconds divided by two
 */
static uint16_t fat32_pack_time(const rtc_datetime_t *dt)
{
    return ((uint16_t)dt->hour << 11) |
           ((uint16_t)dt->minute << 5) | (dt->second / 2);
}

/*
 * Reads the current system clock and checks the FAT32 year range.
 */
static bool fat32_timestamp_current(rtc_datetime_t *dt)
{
    if (!time_get_datetime(dt)) {
        return false;
    }

    return dt->year >= 1980 && dt->year <= 2107;
}

/*
 * Populates creation or modification timestamps.
 *
 * The creation tenths field stores units of 10 milliseconds.
 * With our current one-second clock, values are 0 or 100.
 */
bool fat32_timestamp_now(fat32_directory_entry_t *entry, bool creation)
{
    if (entry == NULL) {
        return false;
    }

    rtc_datetime_t dt;

    if (!fat32_timestamp_current(&dt)) {
        return false;
    }

    uint16_t date = fat32_pack_date(&dt);
    uint16_t time = fat32_pack_time(&dt);

    if (creation) {
        entry->creation_date = date;
        entry->creation_time = time;
        entry->creation_time_tenths = (dt.second % 2) * 100;
        entry->last_access_date = date;
    } else {
        entry->modification_date = date;
        entry->modification_time = time;
    }

    return true;
}

/*
 * Updates the date-only last-access field.
 */
bool fat32_timestamp_access(fat32_directory_entry_t *entry)
{
    if (entry == NULL) {
        return false;
    }

    rtc_datetime_t dt;

    if (!fat32_timestamp_current(&dt)) {
        return false;
    }

    entry->last_access_date = fat32_pack_date(&dt);
    return true;
}

/*
 * Decodes a packed FAT date and optional time into VFS metadata.
 * Rejects zero or invalid dates.
 */
static void fat32_unpack_datetime(uint16_t date, uint16_t time,
                                  bool has_time, vfs_datetime_t *dt)
{
    dt->year = 1980 + ((date >> 9) & 0x7F);
    dt->month = (date >> 5) & 0x0F;
    dt->day = date & 0x1F;
    dt->hour = has_time ? ((time >> 11) & 0x1F) : 0;
    dt->minute = has_time ? ((time >> 5) & 0x3F) : 0;
    dt->second = has_time ? ((time & 0x1F) * 2) : 0;
    dt->has_time = has_time;
    dt->valid = false;

    if (date == 0 || dt->month < 1 || dt->month > 12 ||
        dt->day < 1 || dt->hour > 23 || dt->minute > 59 || dt->second > 59) {
        return;
    }

    static const uint8_t days[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };

    uint8_t max_day = days[dt->month - 1];

    if (dt->month == 2 && (dt->year % 4 == 0) &&
        ((dt->year % 100 != 0) || (dt->year % 400 == 0))) {
        max_day++;
    }

    dt->valid = dt->day <= max_day;
}

/*
 * Converts all three on-disk FAT32 timestamps into VFS metadata.
 *
 * FAT32 stores no time-of-day for last access, so has_time is false.
 */
void fat32_timestamp_decode(const fat32_directory_entry_t *entry,
                            vfs_datetime_t *created, vfs_datetime_t *modified,
                            vfs_datetime_t *accessed)
{
    fat32_unpack_datetime(entry->creation_date, entry->creation_time, true, created);
    fat32_unpack_datetime(entry->modification_date, entry->modification_time, true, modified);
    fat32_unpack_datetime(entry->last_access_date, 0, false, accessed);
}