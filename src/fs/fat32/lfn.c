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
#include <stdbool.h>
#include <partition.h>
#include <mem.h>
#include <stddef.h>
#include <fat32.h>
#include <unicode.h>
#include <str.h>

uint8_t fat32_lfn_checksum(const uint8_t *name)
{
    uint8_t checksum = 0;

    for (uint32_t i = 0; i < FAT32_SHORT_NAME_LENGTH; i++) {
        // Rotate the checksum right by one bit.
        checksum = (checksum >> 1) | (checksum << 7);

        // Add the next short filename character.
        checksum += name[i];
    }

    return checksum;
}

void fat32_lfn_process(fat32_lfn_state_t *state, const fat32_long_directory_entry_t *entry)
{
    uint8_t order = entry->order & FAT32_LFN_ORDER_MASK;

    // Reject invalid long filename entries.
    if (order == 0 || order > FAT32_LFN_MAX_ENTRIES ||
        (entry->order & FAT32_LFN_RESERVED_ORDER_MASK) ||
        entry->type != 0 || entry->first_cluster_low != 0) {
        state->active = false;
        return;
    }

    // Initialise reconstruction when a new sequence begins.
    if (entry->order & FAT32_LFN_LAST_ENTRY) {
        memset(state, 0, sizeof(fat32_lfn_state_t));

        state->active = true;
        state->checksum = entry->checksum;
        state->expected_order = order;
        state->entry_count = order;
    }

    // Validate the sequence order and checksum.
    if (!state->active || order != state->expected_order ||
        entry->checksum != state->checksum) {
        state->active = false;
        return;
    }

    // Calculate the position within the reconstructed filename.
    uint32_t offset = (order - 1) * FAT32_LFN_CHARS_PER_ENTRY;

    memcpy(state->name + offset, entry->name_1, sizeof(entry->name_1));
    offset += sizeof(entry->name_1) / sizeof(uint16_t);

    memcpy(state->name + offset, entry->name_2, sizeof(entry->name_2));
    offset += sizeof(entry->name_2) / sizeof(uint16_t);

    memcpy(state->name + offset, entry->name_3, sizeof(entry->name_3));

    state->expected_order--;
}

bool fat32_lfn_validate(const fat32_lfn_state_t *state, const fat32_directory_entry_t *entry)
{
    if (!state->active || state->expected_order != 0) {
        return false;
    }

    // Verify that the long filename belongs to this directory entry.
    if (state->checksum != fat32_lfn_checksum(entry->name)) {
        return false;
    }

    uint32_t capacity = state->entry_count * FAT32_LFN_CHARS_PER_ENTRY;
    uint32_t length = 0;

    // Determine the actual filename length.
    while (length < capacity && state->name[length] != 0) {
        if (state->name[length] == FAT32_LFN_PADDING) {
            return false;
        }

        length++;
    }

    if (length == 0 || length > FAT32_LFN_MAX_LENGTH) {
        return false;
    }

    // Ensure that all allocated LFN entries are necessary.
    if (length <= capacity - FAT32_LFN_CHARS_PER_ENTRY) {
        return false;
    }

    // Validate any padding following the null terminator.
    for (uint32_t i = length + 1; i < capacity; i++) {
        if (state->name[i] != FAT32_LFN_PADDING) {
            return false;
        }
    }

    return true;
}

bool fat32_format_long_name(const fat32_lfn_state_t *state,
                            const fat32_directory_entry_t *entry, char *name)
{
    if (state == NULL || entry == NULL || name == NULL) {
        return false;
    }

    if (!fat32_lfn_validate(state, entry)) {
        return false;
    }

    uint32_t capacity = state->entry_count * FAT32_LFN_CHARS_PER_ENTRY;
    uint32_t length = 0;

    // Determine the number of UTF-16 code units in the filename.
    while (length < capacity && state->name[length] != 0) {
        length++;
    }

    return utf16_to_utf8(state->name, length, name, FAT32_UTF8_NAME_BUFFER_SIZE);
}


/*
 * Constructs a single FAT32 long filename directory entry.
 *
 * Each entry holds up to 13 UTF-16 code units. The final segment
 * contains a null terminator and 0xFFFF padding when space permits.
 *
 * This function only prepares the entry in memory.
 */
bool fat32_lfn_make_entry(const uint16_t *name, uint32_t name_length,
                         uint8_t order, uint8_t checksum,
                         fat32_long_directory_entry_t *entry)
{
    if (name == NULL || entry == NULL ||
        name_length == 0 || name_length > FAT32_LFN_MAX_LENGTH) {
        return false;
    }

    uint32_t entry_count =
        (name_length + FAT32_LFN_CHARS_PER_ENTRY - 1) /
        FAT32_LFN_CHARS_PER_ENTRY;

    if (order == 0 || order > entry_count ||
        entry_count > FAT32_LFN_MAX_ENTRIES) {
        return false;
    }

    memset(entry, 0, sizeof(*entry));

    entry->order = order;
    entry->attributes = FAT32_ATTR_LONG_NAME;
    entry->type = 0;
    entry->checksum = checksum;
    entry->first_cluster_low = 0;

    if (order == entry_count) {
        entry->order |= FAT32_LFN_LAST_ENTRY;
    }

    uint32_t offset = (order - 1) * FAT32_LFN_CHARS_PER_ENTRY;

    // Initialise all character positions to FAT32 padding.
    uint16_t characters[FAT32_LFN_CHARS_PER_ENTRY];

    for (uint32_t i = 0; i < FAT32_LFN_CHARS_PER_ENTRY; i++) {
        characters[i] = FAT32_LFN_PADDING;
    }

    // Copy this segment of the filename into the entry.
    for (uint32_t i = 0; i < FAT32_LFN_CHARS_PER_ENTRY; i++) {
        uint32_t position = offset + i;

        if (position < name_length) {
            characters[i] = name[position];
        } else if (position == name_length) {
            characters[i] = 0;
        }
    }

    memcpy(entry->name_1, characters, sizeof(entry->name_1));
    memcpy(entry->name_2, characters + 5, sizeof(entry->name_2));
    memcpy(entry->name_3, characters + 11, sizeof(entry->name_3));

    return true;
}
