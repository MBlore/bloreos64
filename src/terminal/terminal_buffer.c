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
#include <terminal/terminal_buffer.h>

/*
 * Returns the physical index of a logical line within the circular buffer.
 */
static uint32_t _line_index(const terminal_buffer_t *buffer, uint32_t index)
{
    return (buffer->head + index) % TERM_MAX_LINES;
}

/*
 * Returns the current line being written to.
 */
static terminal_line_t *_current_line(terminal_buffer_t *buffer)
{
    return &buffer->lines[_line_index(buffer, buffer->count - 1)];
}

/*
 * Appends an empty line to the buffer.
 * When capacity is reached, the oldest line is discarded.
 */
static void _append_line(terminal_buffer_t *buffer, bool continuation)
{
    if (buffer->count == TERM_MAX_LINES) {
        buffer->head = (buffer->head + 1) % TERM_MAX_LINES;
    } else {
        buffer->count++;
    }

    terminal_line_t *line = _current_line(buffer);

    line->length = 0;
    line->continuation = continuation;
}

/*
 * Initializes an empty terminal buffer with one writable line.
 */
void terminal_buffer_init(terminal_buffer_t *buffer)
{
    buffer->head = 0;
    buffer->count = 1;

    buffer->lines[0].length = 0;
    buffer->lines[0].continuation = false;
}

/*
 * Discards all terminal contents and creates an empty writable line.
 */
void terminal_buffer_clear(terminal_buffer_t *buffer)
{
    terminal_buffer_init(buffer);
}

/*
 * Appends characters to the terminal buffer, preserving their colours.
 *
 * Newlines create new logical lines. Lines exceeding the storage width
 * are split into continuation lines without discarding characters.
 */
void terminal_buffer_write(terminal_buffer_t *buffer, const char *data, size_t length,
                           uint32_t foreground, uint32_t background)
{
    if (data == NULL || length == 0) {
        return;
    }

    for (size_t i = 0; i < length; i++) {
        char ch = data[i];

        if (ch == '\0') {
            continue;
        }

        if (ch == '\n') {
            _append_line(buffer, false);
            continue;
        }

        terminal_line_t *line = _current_line(buffer);

        if (line->length == TERM_MAX_COLUMNS) {
            _append_line(buffer, true);
            line = _current_line(buffer);
        }

        terminal_cell_t *cell = &line->cells[line->length++];

        cell->character = ch;
        cell->foreground = foreground;
        cell->background = background;
    }
}

/*
 * Returns a line by its logical index, starting from the oldest line.
 * Returns NULL when the requested index is outside the retained history.
 */
const terminal_line_t *terminal_buffer_get_line(const terminal_buffer_t *buffer, uint32_t index)
{
    if (index >= buffer->count) {
        return NULL;
    }

    return &buffer->lines[_line_index(buffer, index)];
}

/*
 * Returns the number of retained lines, including the current writable line.
 */
uint32_t terminal_buffer_get_count(const terminal_buffer_t *buffer)
{
    return buffer->count;
}