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
#include <terminal.h>
#include <stddef.h>
#include <limine.h>
#include <kernel.h>
#include <stdarg.h>
#include <str.h>
#include <mem.h>
#include <stdbool.h>

typedef struct {
    uint16_t magic;         // Magic bytes for identification.
    uint8_t fontMode;       // PSF font mode.
    uint8_t char_height;  // PSF character size.
} __attribute__((packed)) PSF1_Header;

struct limine_module_request file_request = {
    .id = LIMINE_MODULE_REQUEST,
    .revision = 0};

struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0};

char *glyph_data = NULL;
PSF1_Header *font_header = NULL;
struct limine_framebuffer *frame_buffer = NULL;
uint8_t glyph_padding = 1;
uint32_t max_rows = 0;
uint32_t max_cols = 0;
uint32_t glyph_width = 0;
uint32_t glyph_height = 0;
bool is_ready = false;
uint32_t fgcolor = TERM_DEFAULT_FGCOLOR;
uint32_t bgcolor = 0x0;

// Position of where the text output area rendering is up to.
uint32_t render_x = 0;
uint32_t render_y = 0;

// Position in the input area of where the next key will be written.
uint32_t input_render_x = 0;
uint32_t input_render_y = 0;

// Position of where the cursor is drawn.
uint32_t cursor_x = 0;
uint32_t cursor_y = 0;

char input_str[256];

inline void term_fgcolor(uint32_t color)
{
    fgcolor = color;
}

inline void term_bgcolor(uint32_t color)
{
    bgcolor = color;
}

static inline uint32_t fbindex(uint32_t x, uint32_t y)
{
    return (y * (frame_buffer->pitch / 4)) + x;
}

static inline void _clear_screen()
{
    // Blank the last row.
    uint32_t *fb = frame_buffer->address;
    uint32_t blank_start = fbindex(0, 0);
    uint32_t blank_end = fbindex(frame_buffer->width, frame_buffer->height);
    memset(&fb[blank_start], 0x00, (char*)&fb[blank_end] - (char*)&fb[blank_start]);
}

static inline void _put_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    uint32_t *fb_ptr = frame_buffer->address;
    uint32_t index = fbindex(x, y);
    fb_ptr[index] = color;
}

void _shift_screen_up()
{
    // Shift all rows from the bottom to the top row, overwriting the top most row.
    uint32_t *fb = frame_buffer->address;
    uint32_t start_pixel_index = fbindex(0, glyph_height);
    uint32_t end_pixel_index = fbindex(frame_buffer->width, frame_buffer->height);

    memmove(
        frame_buffer->address,
        &fb[start_pixel_index],
        (char*)&fb[end_pixel_index] - (char*)&fb[start_pixel_index]);
    
    // Blank the last row.
    uint32_t blank_start = fbindex(0, frame_buffer->height - glyph_height);
    uint32_t blank_end = fbindex(frame_buffer->width, frame_buffer->height);
    memset(&fb[blank_start], 0x00, (char*)&fb[blank_end] - (char*)&fb[blank_start]);
}

/*
 * Loads the PSF font and sets global pointers ready for rendering.
*/
void _load_font()
{
    for (uint64_t i = 0; i < file_request.response->module_count; i++) {
        if (memcmp("/Font.psf", file_request.response->modules[i]->path, 9) == 0) {
            
            font_header = (PSF1_Header*)file_request.response->modules[i]->address;
            
            // Check for version 1 of the PSF file format.
            // Version 2 has much more header information as detailed on the OS Wiki.
            if (font_header->magic != 0x0436) {
                kprintf("FATAL: Failed to load font.");
                hcf();
            }

            //int numGlyphs = header->fontMode & 0x1 ? 512 : 256;
            //int width = 8;
            //int bytesPerRow = 1;
            glyph_data = (char*)font_header + sizeof(PSF1_Header);
            break;
        }
    }
}

/*
 * Renders a font glyph at the specified pixel location. 
*/
void _render_glyph(char ch, uint32_t x, uint32_t y)
{
    uint8_t bytesPerGlyph = 1 * font_header->char_height;
    uint32_t dataIndex = ch * bytesPerGlyph;

    // Render the glyph.
    for (int glyphRow = 0; glyphRow < font_header->char_height; glyphRow++) {
        // Read 1 byte for this row.
        char rowData = glyph_data[dataIndex++];

        for (int bitIndex = 0; bitIndex < 8; bitIndex++) {
            uint8_t bit_mask = 1 << (bitIndex % 8);
            if (rowData & bit_mask) {
                // Foreground.
                _put_pixel(x + (8 - bitIndex), y + glyphRow, fgcolor);
            } else {
                // Background.
                _put_pixel(x + (8 - bitIndex), y + glyphRow, bgcolor);
            }
        }
    }
}

/*
 * Clears the input cursor. 
*/
void _clear_cursor()
{
    for (uint32_t i = 0; i < glyph_height; i++) {
        _put_pixel(cursor_x, cursor_y+i, bgcolor);
    }
}
/*
 * Renders the input cursor. 
*/
void _render_cursor()
{
    // Draw a line "|" for the cursor.
    for (uint32_t i = 0; i < glyph_height; i++) {
        _put_pixel(input_render_x, input_render_y+i, TERM_CURSOR_COLOR);
    }

    cursor_x = input_render_x;
    cursor_y = input_render_y;
}

/*
 * Draws the term input line. Triggered when the text output scrolls or writes a new line char.
*/
void _render_input_line()
{
    // If the render cursor X is 0, the line hasn't been written to yet.
    // If it is > 0, the line is being written so we use the line underneath as the input line.
    input_render_x = 0;
    input_render_y = render_x > 0 ? render_y + glyph_height : render_y;

    char buffer[] = "cmd> ";

    char *ch = &buffer[0];

    term_fgcolor(0x44AAFF);

    do {
        _render_glyph(*ch, input_render_x, input_render_y);
        input_render_x += glyph_width;

        // TODO: Line breaks to next line, aka, multi-line input rendering.
        ch++;
    } while(*ch != '\0');

    term_fgcolor(TERM_DEFAULT_FGCOLOR);

    _render_cursor();
}

/*
 * Moves the rendering cursor to the next line. If the cursor exceeds the height of the screen,
 * this triggers a scroll of all lines above. 
*/
void _move_to_next_line()
{
    if (render_x == 0) {
        // Blank the input from this line.
        uint32_t *fb = frame_buffer->address;
        uint32_t blank_start =fbindex(0, render_y);
        uint32_t blank_end = fbindex(frame_buffer->width, render_y + glyph_height);
        memset(&fb[blank_start], 0x00, (char*)&fb[blank_end] - (char*)&fb[blank_start]);
    }

    render_x = 0;
    render_y += glyph_height;

    // Detect bottom of screen.
    if (render_y > frame_buffer->height - glyph_height) {
        _shift_screen_up();
        render_y -= glyph_height;
    }

    // Blank the new line as the input was here.
    uint32_t *fb = frame_buffer->address;
    uint32_t blank_start =fbindex(0, render_y);
    uint32_t blank_end = fbindex(frame_buffer->width, render_y + glyph_height);
    memset(&fb[blank_start], 0x00, (char*)&fb[blank_end] - (char*)&fb[blank_start]);

    _render_input_line();
}

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

    // Make sure the line is clear before we start writing in to it.
    if (render_x == 0) {
        uint32_t *fb = frame_buffer->address;
        uint32_t blank_start = fbindex(0, render_y);
        uint32_t blank_end = fbindex(frame_buffer->width, render_y + glyph_height);
        memset(&fb[blank_start], 0x00, (char*)&fb[blank_end] - (char*)&fb[blank_start]);
    }
    
    // Write text to the frame buffer using our glyph data.
    char *ch = &buffer[0];

    do {
        // Advance the cursor for new lines.
        if (*ch == '\n') {
            _move_to_next_line();

            ch++;
            continue;
        }

        _render_glyph(*ch, render_x, render_y);

        // Advance the rendering position.
        render_x += 8 + glyph_padding;

        if (render_x > frame_buffer->width - glyph_width) {
            _move_to_next_line();
        }

        ch++;
    } while (*ch != '\0');
}

void term_init()
{
    frame_buffer = framebuffer_request.response->framebuffers[0];

    _load_font();

    max_rows = frame_buffer->height / (font_header->char_height + glyph_padding);
    max_cols = frame_buffer->width / (8 + glyph_padding);

    glyph_width = 8 + glyph_padding;
    glyph_height = font_header->char_height + glyph_padding;

    kprintf("Font: %dx%d\n", glyph_width - glyph_padding, glyph_height - glyph_padding);
    kprintf("Resolution: %dx%d\n", frame_buffer->width, frame_buffer->height);
    kprintf("BPP: %d\n", frame_buffer->bpp);
    kprintf("Pitch: %d\n", frame_buffer->pitch);

    _render_input_line();
    
    is_ready = true;
}

/*
 * Executes a command from the user input. 
*/
void _handle_cmd()
{
    if (strcmp(input_str, "hello") == 0) {
        tprintf("World!\n");
    } else if (strcmp(input_str, "cls") == 0) {
        _clear_screen();
        render_x = render_y = input_render_x = input_render_y = cursor_x = cursor_y = 0;
        _render_input_line();
    } else {
        tprintf("Unknown command.\n");
    }
}

/*
 * Handles a key event from the keyboard driver. We allow the user to type in to the input area.
*/
void term_keyevent(KeyEvent_t *ke)
{
    if (ke->event_type == PS2_KEYDOWN && ke->scan_code == PS2_SCANCODE_ENTER) {
        // Process the input buffer.
        tprintf("cmd> ");
        tprintf(input_str); // Echo the command to the output.
        tprintf("\n");
        _handle_cmd();

        input_str[0] = '\0';

        return;
    }

    if (ke->event_type == PS2_KEYDOWN && ke->scan_code == PS2_SCANCODE_BACKSPACE) {
        if (input_str[0] != '\0') {
            input_str[strlen(input_str)-1] = '\0';

            _clear_cursor();

            // Clear the glyph.
            uint32_t *fb = frame_buffer->address;
            for (uint32_t y = 0; y < glyph_height; y++) {
                uint32_t blank_start = fbindex(input_render_x - glyph_width, input_render_y+y);
                uint32_t blank_end = fbindex(input_render_x, input_render_y+y);
                memset(&fb[blank_start], 0, (char*)&fb[blank_end] - (char*)&fb[blank_start]);
            }

            input_render_x -= glyph_width;

            _render_cursor();
            return;
        }
    }

    if (ke->event_type == 0 && ke->ascii != 0) {
        // Key down.
        _render_glyph(ke->ascii, input_render_x, input_render_y);
        input_render_x += glyph_width;

        // Append to buffer.
        size_t len = strlen(input_str);
        input_str[len] = ke->ascii;
        input_str[len+1] = '\0';

        _clear_cursor();
        _render_cursor();
    }
}