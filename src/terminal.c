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
uint32_t cursor_x = 0;
uint32_t cursor_y = 0;

uint32_t glyph_width = 0;
uint32_t glyph_height = 0;

static inline uint32_t fbindex(uint32_t x, uint32_t y)
{
    return (y * (frame_buffer->pitch / 4)) + x;
}

static inline void _put_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    uint32_t *fb_ptr = frame_buffer->address;
    uint32_t index = fbindex(x, y);
    fb_ptr[index] = color;
}

void _shift_screen_up()
{
    uint32_t *fb = frame_buffer->address;
    uint32_t start_pixel_index = (glyph_height * (frame_buffer->pitch / 4));
    uint32_t end_pixel_index = (frame_buffer->height * (frame_buffer->pitch / 4)) + frame_buffer->width;

    memmove(frame_buffer->address, (void*)&fb[start_pixel_index], (size_t)end_pixel_index - start_pixel_index * 4);

    // Blank the last row.
    uint32_t blank_start = fbindex(0, frame_buffer->height - glyph_height);
    uint32_t blank_end = fbindex(frame_buffer->width, frame_buffer->height);
    memset(&fb[blank_start], 0, (blank_end - blank_start) * 4);
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

void tprintf(const char format[], ...)
{
    char buffer[256];

    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    // Write text to the frame buffer using our glyph data.
    uint64_t dataIndex = 0;
    char *ch = &buffer[0];
    uint8_t bytesPerGlyph = 1 * font_header->char_height;

    do {
        // Point to the glyph in memory according to the char we are currently processing.
        dataIndex = *ch * bytesPerGlyph;

        // Advance the cursor for new lines.
        if (*ch == '\n') {
            cursor_x = 0;
            cursor_y += font_header->char_height + glyph_padding;

            // Detect bottom of screen.
            if (cursor_y > frame_buffer->height - (font_header->char_height + glyph_padding)) {
                _shift_screen_up();
                cursor_y -= font_header->char_height + glyph_padding;
            }

            ch++;
            continue;
        }

        // Render the glyph.
        for (int glyphRow = 0; glyphRow < font_header->char_height; glyphRow++) {
            // Read 1 byte for this row.
            char rowData = glyph_data[dataIndex++];

            for (int bitIndex = 0; bitIndex < 8; bitIndex++) {
                uint8_t bit_mask = 1 << (bitIndex % 8);
                if (rowData & bit_mask) {
                    // Foreground.
                    _put_pixel(cursor_x + (8 - bitIndex), cursor_y + glyphRow, 0xFFFFFF);
                } else {
                    // Background.
                    _put_pixel(cursor_x + (8 - bitIndex), cursor_y + glyphRow, 0x000000);
                }
            }
        }

        // Advance the cursor.
        cursor_x += 8 + glyph_padding;

        if (cursor_x > frame_buffer->width - glyph_width) {
            cursor_x = 0;
            cursor_y += glyph_height;

            // Detect bottom of screen.
            if (cursor_y > frame_buffer->height - glyph_height) {
                _shift_screen_up();
                cursor_y -= glyph_height;
            }
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
}