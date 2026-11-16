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
#include <unicode.h>

#define UTF8_ONE_BYTE_MAX 0x7F
#define UTF8_TWO_BYTE_MAX 0x7FF
#define UTF8_THREE_BYTE_MAX 0xFFFF
#define UTF8_FOUR_BYTE_MAX 0x10FFFF

#define UTF8_TWO_BYTE_PREFIX 0xC0
#define UTF8_THREE_BYTE_PREFIX 0xE0
#define UTF8_FOUR_BYTE_PREFIX 0xF0
#define UTF8_CONTINUATION_PREFIX 0x80
#define UTF8_CONTINUATION_MASK 0x3F
#define UTF8_CONTINUATION_BITS 6

#define UTF16_SURROGATE_MIN 0xD800
#define UTF16_SURROGATE_MAX 0xDFFF

#define UTF16_HIGH_SURROGATE_MIN 0xD800
#define UTF16_HIGH_SURROGATE_MAX 0xDBFF
#define UTF16_LOW_SURROGATE_MIN 0xDC00
#define UTF16_LOW_SURROGATE_MAX 0xDFFF
#define UTF16_SURROGATE_BITS 10
#define UTF16_SUPPLEMENTARY_BASE 0x10000

static size_t utf8_encode(uint32_t codepoint, char *destination, size_t capacity)
{
    // Reject invalid Unicode code points.
    if (codepoint > UTF8_FOUR_BYTE_MAX ||
        (codepoint >= UTF16_SURROGATE_MIN && codepoint <= UTF16_SURROGATE_MAX)) {
        return 0;
    }

    if (codepoint <= UTF8_ONE_BYTE_MAX && capacity >= 1) {
        destination[0] = codepoint;
        return 1;
    }

    if (codepoint <= UTF8_ONE_BYTE_MAX) {
        return 0;
    }

    if (codepoint <= UTF8_TWO_BYTE_MAX) {
        if (capacity < 2) {
            return 0;
        }

        destination[0] = UTF8_TWO_BYTE_PREFIX | (codepoint >> UTF8_CONTINUATION_BITS);
        destination[1] = UTF8_CONTINUATION_PREFIX | (codepoint & UTF8_CONTINUATION_MASK);

        return 2;
    }

    if (codepoint <= UTF8_THREE_BYTE_MAX) {
        if (capacity < 3) {
            return 0;
        }

        destination[0] = UTF8_THREE_BYTE_PREFIX | (codepoint >> 12);
        destination[1] = UTF8_CONTINUATION_PREFIX | ((codepoint >> 6) & UTF8_CONTINUATION_MASK);
        destination[2] = UTF8_CONTINUATION_PREFIX | (codepoint & UTF8_CONTINUATION_MASK);

        return 3;
    }

    if (capacity < 4) {
        return 0;
    }

    destination[0] = UTF8_FOUR_BYTE_PREFIX | (codepoint >> 18);
    destination[1] = UTF8_CONTINUATION_PREFIX | ((codepoint >> 12) & UTF8_CONTINUATION_MASK);
    destination[2] = UTF8_CONTINUATION_PREFIX | ((codepoint >> 6) & UTF8_CONTINUATION_MASK);
    destination[3] = UTF8_CONTINUATION_PREFIX | (codepoint & UTF8_CONTINUATION_MASK);

    return 4;
}

bool utf16_to_utf8(const uint16_t *source, size_t length, char *destination, size_t capacity)
{
    if (source == NULL || destination == NULL || capacity == 0) {
        return false;
    }

    size_t position = 0;
    destination[0] = '\0';

    for (size_t i = 0; i < length; i++) {
        uint32_t codepoint = source[i];

        // Decode a UTF-16 surrogate pair.
        if (codepoint >= UTF16_HIGH_SURROGATE_MIN && codepoint <= UTF16_HIGH_SURROGATE_MAX) {
            if (i + 1 >= length) {
                return false;
            }

            uint32_t low = source[++i];

            if (low < UTF16_LOW_SURROGATE_MIN || low > UTF16_LOW_SURROGATE_MAX) {
                return false;
            }

            codepoint = UTF16_SUPPLEMENTARY_BASE +
                        ((codepoint - UTF16_HIGH_SURROGATE_MIN) << UTF16_SURROGATE_BITS) +
                        (low - UTF16_LOW_SURROGATE_MIN);
        } else if (codepoint >= UTF16_LOW_SURROGATE_MIN && codepoint <= UTF16_LOW_SURROGATE_MAX) {
            // Reject a low surrogate without a preceding high surrogate.
            return false;
        }

        // Encode the code point, reserving space for the null terminator.
        size_t written = utf8_encode(codepoint, destination + position, capacity - position - 1);

        if (written == 0) {
            return false;
        }

        position += written;
        destination[position] = '\0';
    }

    return true;
}