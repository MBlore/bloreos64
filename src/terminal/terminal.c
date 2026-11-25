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
#include <terminal/terminal.h>
#include <terminal/terminal_buffer.h>
#include <stddef.h>
#include <limine.h>
#include <kernel.h>
#include <stdarg.h>
#include <str.h>
#include <mem.h>
#include <stdbool.h>
#include <cpu.h>
#include <time.h>
#include <commands.h>

#define TERM_PROMPT_COLOR 0x44AAFF

typedef struct {
    uint16_t magic;
    uint8_t fontMode;
    uint8_t char_height;
} __attribute__((packed)) PSF1_Header;

struct limine_module_request file_request = {
    .id = LIMINE_MODULE_REQUEST,
    .revision = 0
};

struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

struct limine_framebuffer *frame_buffer = NULL;

static char *glyph_data = NULL;
static PSF1_Header *font_header = NULL;

static uint8_t glyph_padding = 1;
static uint32_t glyph_width = 0;
static uint32_t glyph_height = 0;
static uint32_t max_rows = 0;
static uint32_t max_cols = 0;

static uint32_t fgcolor = TERM_DEFAULT_FGCOLOR;
static uint32_t bgcolor = 0x000000;

static bool is_ready = false;
static bool terminal_visible = false;
static bool cursor_visible = true;
static bool prompt_active = false;

static bool left_shift_down = false;
static bool right_shift_down = false;

static char input_str[256];

static uint32_t scroll_offset = 0;
static terminal_buffer_t terminal_buffer;

static uint32_t cursor_x = 0;
static uint32_t cursor_y = 0;
static bool cursor_on_screen = false;

static uint32_t viewport_x = 0;
static uint32_t viewport_y = 0;
static uint32_t viewport_width = 0;
static uint32_t viewport_height = 0;

/*
 * Calculates the framebuffer index for a pixel.
 */
static inline uint64_t fbindex(uint32_t x, uint32_t y)
{
    return ((uint64_t)y * (frame_buffer->pitch / 4)) + x;
}

/*
 * Writes a pixel to the framebuffer.
 */
static inline void _put_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    if (x >= viewport_width || y >= viewport_height) {
        return;
    }

    uint32_t *fb = frame_buffer->address;
    fb[fbindex(viewport_x + x, viewport_y + y)] = color;
}

/*
 * Loads the PSF font supplied by the bootloader.
 */
static void _load_font(void)
{
    if (file_request.response != NULL) {
        for (uint64_t i = 0; i < file_request.response->module_count; i++) {
            struct limine_file *module = file_request.response->modules[i];

            if (strcmp(module->path, "/Font.psf") != 0) {
                continue;
            }

            if (module->size < sizeof(PSF1_Header)) {
                break;
            }

            font_header = (PSF1_Header *)module->address;

            if (font_header->magic != 0x0436 || font_header->char_height == 0) {
                break;
            }

            if (module->size < sizeof(PSF1_Header) + (256ULL * font_header->char_height)) {
                break;
            }

            glyph_data = (char *)font_header + sizeof(PSF1_Header);
            return;
        }
    }

    kprintf("FATAL: Failed to load font.\n");
    hcf();
}

/*
 * Renders a character cell using its supplied foreground and background colours.
 */
static void _render_glyph(char ch, uint32_t x, uint32_t y, uint32_t foreground, uint32_t background)
{
    uint32_t data_index = (uint8_t)ch * font_header->char_height;

    for (uint32_t row = 0; row < glyph_height; row++) {
        uint8_t row_data = 0;

        if (row < font_header->char_height) {
            row_data = glyph_data[data_index++];
        }

        for (uint32_t col = 0; col < glyph_width; col++) {
            uint32_t color = background;

            if (col > 0 && col <= 8 && (row_data & (1 << (8 - col)))) {
                color = foreground;
            }

            _put_pixel(x + col, y + row, color);
        }
    }
}

/*
 * Clears the framebuffer without modifying the terminal history.
 */
static void _clear_render_area(void)
{
    uint32_t *fb = frame_buffer->address;

    for (uint32_t y = 0; y < viewport_height; y++) {
        uint32_t *row = &fb[fbindex(viewport_x, viewport_y + y)];

        for (uint32_t x = 0; x < viewport_width; x++) {
            row[x] = bgcolor;
        }
    }
}

/*
 * Returns the number of visual rows occupied by a logical line.
 */
static uint32_t _get_visual_rows(uint32_t length)
{
    if (length == 0) {
        return 1;
    }

    return (length + max_cols - 1) / max_cols;
}

/*
 * Returns the length of the editable prompt, including the cursor cell.
 */
static uint32_t _get_prompt_length(void)
{
    if (!prompt_active) {
        return 0;
    }

    return strlen(commands_get_cwd()) + 3 + strlen(input_str) + 1;
}

/*
 * Calculates the total number of visual rows occupied by the terminal.
 *
 * Continuation lines are treated as part of the preceding logical line.
 */
static uint32_t _get_total_rows(void)
{
    uint32_t rows = 0;
    uint32_t length = 0;
    uint32_t count = terminal_buffer_get_count(&terminal_buffer);

    for (uint32_t i = 0; i < count; i++) {
        const terminal_line_t *line = terminal_buffer_get_line(&terminal_buffer, i);

        if (i != 0 && !line->continuation) {
            rows += _get_visual_rows(length);
            length = 0;
        }

        length += line->length;
    }

    length += _get_prompt_length();

    return rows + _get_visual_rows(length);
}

/*
 * Draws a character at a logical terminal position if it is within the visible area.
 * Advances the visual position and wraps when necessary.
 */
static void _render_cell(char ch, uint32_t foreground, uint32_t background,
                         uint32_t *row, uint32_t *col, uint32_t first_row)
{
    if (*col == max_cols) {
        (*row)++;
        *col = 0;
    }

    if (*row >= first_row && *row - first_row < max_rows) {
        _render_glyph(ch, *col * glyph_width, (*row - first_row) * glyph_height,
                      foreground, background);
    }

    (*col)++;
}

/*
 * Renders a string at the current logical terminal position.
 */
static void _render_string(const char *str, uint32_t foreground, uint32_t background,
                           uint32_t *row, uint32_t *col, uint32_t first_row)
{
    while (*str != '\0') {
        _render_cell(*str++, foreground, background, row, col, first_row);
    }
}

/*
 * Updates the cursor without redrawing the terminal.
 */
static void _draw_cursor(void)
{
    if (!terminal_visible || !prompt_active || !cursor_on_screen) {
        return;
    }

    uint32_t color = cursor_visible ? TERM_CURSOR_COLOR : bgcolor;

    for (uint32_t i = 0; i < glyph_height; i++) {
        _put_pixel(cursor_x, cursor_y + i, color);
    }
}

/*
 * Renders the visible portion of the terminal history and editable prompt.
 */
static void _render_terminal(void)
{
    if (!is_ready || !terminal_visible || max_cols == 0 || max_rows == 0) {
        return;
    }

    cursor_on_screen = false;

    uint32_t total_rows = _get_total_rows();
    uint32_t max_offset = total_rows > max_rows ? total_rows - max_rows : 0;

    if (scroll_offset > max_offset) {
        scroll_offset = max_offset;
    }

    uint32_t first_row = max_offset - scroll_offset;

    _clear_render_area();

    uint32_t row = 0;
    uint32_t col = 0;
    uint32_t count = terminal_buffer_get_count(&terminal_buffer);

    for (uint32_t i = 0; i < count; i++) {
        const terminal_line_t *line = terminal_buffer_get_line(&terminal_buffer, i);

        if (i != 0 && !line->continuation) {
            row++;
            col = 0;
        }

        for (uint32_t j = 0; j < line->length; j++) {
            const terminal_cell_t *cell = &line->cells[j];

            _render_cell(cell->character, cell->foreground, cell->background,
                         &row, &col, first_row);
        }
    }

    if (!prompt_active) {
        return;
    }

    _render_string(commands_get_cwd(), TERM_PROMPT_COLOR, bgcolor, &row, &col, first_row);
    _render_string(" $ ", TERM_PROMPT_COLOR, bgcolor, &row, &col, first_row);
    _render_string(input_str, TERM_DEFAULT_FGCOLOR, bgcolor, &row, &col, first_row);

    if (col == max_cols) {
        row++;
        col = 0;
    }

    if (scroll_offset == 0 && row >= first_row && row - first_row < max_rows) {
        cursor_x = col * glyph_width;
        cursor_y = (row - first_row) * glyph_height;
        cursor_on_screen = true;

        _draw_cursor();
    }
}

/*
 * Sets the terminal rendering area and recalculates its character dimensions.
 *
 * The retained text buffer is unaffected by viewport changes.
 */
void term_set_viewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    if (!is_ready || glyph_width == 0 || glyph_height == 0) {
        return;
    }

    if (x >= frame_buffer->width || y >= frame_buffer->height) {
        return;
    }

    if (width > frame_buffer->width - x || height > frame_buffer->height - y) {
        return;
    }

    if (width < glyph_width || height < glyph_height) {
        return;
    }

    bool interrupt_enabled = set_interrupt_state(false);

    viewport_x = x;
    viewport_y = y;
    viewport_width = width;
    viewport_height = height;

    max_cols = viewport_width / glyph_width;
    max_rows = viewport_height / glyph_height;

    scroll_offset = 0;
    cursor_on_screen = false;

    _render_terminal();

    set_interrupt_state(interrupt_enabled);
}

/*
 * Sets the foreground colour for subsequent terminal output.
 */
void term_fgcolor(uint32_t color)
{
    fgcolor = color;
}

/*
 * Sets the background colour for subsequent terminal output.
 */
void term_bgcolor(uint32_t color)
{
    bgcolor = color;
}

/*
 * Controls terminal rendering without affecting retained text.
 */
void term_set_visible(bool visible)
{
    bool interrupt_enabled = set_interrupt_state(false);

    terminal_visible = visible;

    if (visible) {
        _render_terminal();
    } else {
        cursor_on_screen = false;
    }

    set_interrupt_state(interrupt_enabled);
}

/*
 * Initialises the terminal buffer, framebuffer and font.
 */
void term_init(void)
{
    terminal_buffer_init(&terminal_buffer);

    frame_buffer = framebuffer_request.response->framebuffers[0];

    _load_font();

    glyph_width = 8 + glyph_padding;
    glyph_height = font_header->char_height + glyph_padding;

    viewport_x = 0;
    viewport_y = 0;
    viewport_width = frame_buffer->width;
    viewport_height = frame_buffer->height;

    max_rows = viewport_height / glyph_height;
    max_cols = viewport_width / glyph_width;

    kprintf("Font: %dx%d\n", glyph_width - glyph_padding, glyph_height - glyph_padding);
    kprintf("Resolution: %dx%d\n", frame_buffer->width, frame_buffer->height);
    kprintf("BPP: %d\n", frame_buffer->bpp);
    kprintf("Pitch: %d\n", frame_buffer->pitch);

    is_ready = true;
}

/*
 * Toggles the visibility of the input cursor.
 */
void term_cblink(void)
{
    if (!is_ready || !terminal_visible || !prompt_active) {
        return;
    }

    bool interrupt_enabled = set_interrupt_state(false);

    cursor_visible = !cursor_visible;

    _draw_cursor();

    set_interrupt_state(interrupt_enabled);
}

/*
 * Displays the interactive prompt after the current terminal output.
 */
void term_show_prompt(void)
{
    if (!is_ready) {
        return;
    }

    bool interrupt_enabled = set_interrupt_state(false);

    uint32_t count = terminal_buffer_get_count(&terminal_buffer);
    const terminal_line_t *line = terminal_buffer_get_line(&terminal_buffer, count - 1);

    if (line->length != 0) {
        terminal_buffer_write(&terminal_buffer, "\n", 1, fgcolor, bgcolor);
    }

    prompt_active = true;
    cursor_visible = true;
    scroll_offset = 0;

    _render_terminal();

    set_interrupt_state(interrupt_enabled);
}

/*
 * Clears the terminal history and resets the scrollback position.
 */
void term_clear(void)
{
    bool interrupt_enabled = set_interrupt_state(false);

    terminal_buffer_clear(&terminal_buffer);

    scroll_offset = 0;

    _render_terminal();

    set_interrupt_state(interrupt_enabled);
}

/*
 * Executes the command contained in the input buffer.
 */
static void _handle_cmd(void)
{
    if (strcmp(input_str, "cls") == 0) {
        term_clear();
    } else if (strcmp(input_str, "time") == 0) {
        tprintf("Kernel Time: %lums\n", time_get_uptime_ms());
    } else if (strcmp(input_str, "stime") == 0) {
        tprintf("Kernel Time: %lusecs\n", time_get_uptime_ms() / 1000);
    } else if (strcmp(input_str, "sleep") == 0) {
        uint64_t before = time_get_uptime_ms();

        time_sleep_ms(60000);

        uint64_t after = time_get_uptime_ms();

        tprintf("Elapsed: %d ms\n", after - before);
    } else if (!commands_execute(input_str)) {
        tprintf("Unknown command.\n");
    }
}

/*
 * Commits the editable prompt and command to the terminal history.
 */
static void _commit_input(void)
{
    const char *cwd = commands_get_cwd();

    terminal_buffer_write(&terminal_buffer, cwd, strlen(cwd), TERM_PROMPT_COLOR, bgcolor);
    terminal_buffer_write(&terminal_buffer, " $ ", 3, TERM_PROMPT_COLOR, bgcolor);
    terminal_buffer_write(&terminal_buffer, input_str, strlen(input_str), TERM_DEFAULT_FGCOLOR, bgcolor);
    terminal_buffer_write(&terminal_buffer, "\n", 1, TERM_DEFAULT_FGCOLOR, bgcolor);

    prompt_active = false;
    cursor_on_screen = false;
    scroll_offset = 0;
}

/*
 * Handles keyboard events and updates the editable command input.
 */
void term_keyevent(KeyEvent_t *ke)
{
    if (ke->scan_code == 42) {
        left_shift_down = (ke->event_type == PS2_KEYDOWN);
        return;
    }

    if (ke->scan_code == 170) {
        left_shift_down = false;
        return;
    }

    if (ke->scan_code == 54) {
        right_shift_down = (ke->event_type == PS2_KEYDOWN);
        return;
    }

    if (ke->scan_code == 182) {
        right_shift_down = false;
        return;
    }

    if (!is_ready || !prompt_active || ke->event_type != PS2_KEYDOWN) {
        return;
    }

    if (ke->scan_code == PS2_SCANCODE_ENTER) {
        bool interrupt_enabled = set_interrupt_state(false);

        _commit_input();
        _render_terminal();

        set_interrupt_state(interrupt_enabled);

        // Commands must execute with their normal interrupt state.
        _handle_cmd();

        input_str[0] = '\0';

        term_show_prompt();

        return;
    }

    if (ke->scan_code == PS2_SCANCODE_BACKSPACE) {
        size_t len = strlen(input_str);

        if (len != 0) {
            bool interrupt_enabled = set_interrupt_state(false);

            input_str[len - 1] = '\0';

            scroll_offset = 0;
            cursor_visible = true;

            _render_terminal();

            set_interrupt_state(interrupt_enabled);
        }

        return;
    }

    if (ke->ascii != 0) {
        char ascii = ke->ascii;
        bool shift = left_shift_down || right_shift_down;

        if (shift) {
            if (ascii >= 'a' && ascii <= 'z') {
                ascii -= ('a' - 'A');
            } else if (ke->scan_code == 12) {
                ascii = '_';
            }
        }

        size_t len = strlen(input_str);

        if (len >= sizeof(input_str) - 1) {
            return;
        }

        bool interrupt_enabled = set_interrupt_state(false);

        input_str[len] = ascii;
        input_str[len + 1] = '\0';

        scroll_offset = 0;
        cursor_visible = true;

        _render_terminal();

        set_interrupt_state(interrupt_enabled);
    }
}

/*
 * Appends an explicitly sized byte sequence to the terminal history.
 *
 * Output is retained even when the terminal is not visible.
 */
void term_write(const char *data, size_t length)
{
    if (!is_ready || data == NULL || length == 0) {
        return;
    }

    bool interrupt_enabled = set_interrupt_state(false);

    terminal_buffer_write(&terminal_buffer, data, length, fgcolor, bgcolor);

    _render_terminal();

    set_interrupt_state(interrupt_enabled);
}

/*
 * Formats a string and appends it to the terminal history.
 */
void tprintf(const char format[], ...)
{
    if (!is_ready) {
        return;
    }

    char buffer[256];

    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    term_write(buffer, strlen(buffer));
}