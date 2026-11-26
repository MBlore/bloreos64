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
#include <gfx/gfx.h>
#include <limine.h>
#include <kernel.h>
#include <str.h>
#include <stddef.h>

#define GLYPH_PADDING 1
#define GLYPH_WIDTH (8 + GLYPH_PADDING)

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

static const uint8_t *glyph_data = NULL;
static const PSF1_Header *font_header = NULL;

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

            font_header = (const PSF1_Header *)module->address;

            if (font_header->magic != 0x0436 || font_header->char_height == 0) {
                break;
            }

            if (module->size < sizeof(PSF1_Header) + (256ULL * font_header->char_height)) {
                break;
            }

            glyph_data = (const uint8_t *)font_header + sizeof(PSF1_Header);
            return;
        }
    }

    kprintf("FATAL: Failed to load font.\n");
    hcf();
}

/*
 * Initialises the framebuffer and font using bootloader-provided resources.
 *
 * This function must not depend on memory allocation, interrupts,
 * filesystem drivers or any other kernel subsystem.
 */
void gfx_init(void)
{
    if (framebuffer_request.response == NULL ||
        framebuffer_request.response->framebuffer_count == 0) {
        kprintf("FATAL: No framebuffer available.\n");
        hcf();
    }

    frame_buffer = framebuffer_request.response->framebuffers[0];

    if (frame_buffer->bpp != 32) {
        kprintf("FATAL: Unsupported framebuffer format.\n");
        hcf();
    }

    _load_font();

    kprintf("Font: %dx%d\n", 8, font_header->char_height);
    kprintf("Resolution: %dx%d\n", frame_buffer->width, frame_buffer->height);
    kprintf("BPP: %d\n", frame_buffer->bpp);
    kprintf("Pitch: %d\n", frame_buffer->pitch);
}

/*
 * Returns the framebuffer width in pixels.
 */
uint32_t gfx_get_width(void)
{
    return frame_buffer->width;
}

/*
 * Returns the framebuffer height in pixels.
 */
uint32_t gfx_get_height(void)
{
    return frame_buffer->height;
}

/*
 * Returns the width of a character cell, including padding.
 */
uint32_t gfx_get_glyph_width(void)
{
    return GLYPH_WIDTH;
}

/*
 * Returns the height of a character cell, including padding.
 */
uint32_t gfx_get_glyph_height(void)
{
    return font_header->char_height + GLYPH_PADDING;
}

/*
 * Writes a pixel using absolute framebuffer coordinates.
 */
void gfx_put_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    if (x >= frame_buffer->width || y >= frame_buffer->height) {
        return;
    }

    uint32_t *row = (void *)((uint8_t *)frame_buffer->address + y * frame_buffer->pitch);

    row[x] = color;
}

/*
 * Fills a rectangular area, clipping it to the framebuffer boundaries.
 */
void gfx_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color)
{
    if (x >= frame_buffer->width || y >= frame_buffer->height) {
        return;
    }

    if (width > frame_buffer->width - x) {
        width = frame_buffer->width - x;
    }

    if (height > frame_buffer->height - y) {
        height = frame_buffer->height - y;
    }

    for (uint32_t row = 0; row < height; row++) {
        uint32_t *dest = (void *)((uint8_t *)frame_buffer->address + (y + row) * frame_buffer->pitch);

        for (uint32_t col = 0; col < width; col++) {
            dest[x + col] = color;
        }
    }
}

/*
 * Draws a one-pixel rectangular outline.
 */
void gfx_draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color)
{
    if (width == 0 || height == 0) {
        return;
    }

    if (x >= frame_buffer->width || y >= frame_buffer->height) {
        return;
    }

    gfx_fill_rect(x, y, width, 1, color);
    gfx_fill_rect(x, y, 1, height, color);

    if (height <= frame_buffer->height - y) {
        gfx_fill_rect(x, y + height - 1, width, 1, color);
    }

    if (width <= frame_buffer->width - x) {
        gfx_fill_rect(x + width - 1, y, 1, height, color);
    }
}

/*
 * Fills the entire framebuffer with the supplied colour.
 */
void gfx_clear(uint32_t color)
{
    gfx_fill_rect(0, 0, gfx_get_width(), gfx_get_height(), color);
}

/*
 * Draws a character cell using explicit foreground and background colours.
 */
void gfx_draw_glyph(char ch, uint32_t x, uint32_t y, uint32_t foreground, uint32_t background)
{
    if (x >= frame_buffer->width || y >= frame_buffer->height) {
        return;
    }

    uint32_t data_index = (uint8_t)ch * font_header->char_height;
    uint32_t height = gfx_get_glyph_height();

    for (uint32_t row = 0; row < height; row++) {
        uint8_t row_data = 0;

        if (row < font_header->char_height) {
            row_data = glyph_data[data_index++];
        }

        for (uint32_t col = 0; col < GLYPH_WIDTH; col++) {
            uint32_t color = background;

            if (col > 0 && col <= 8 && (row_data & (1 << (8 - col)))) {
                color = foreground;
            }

            gfx_put_pixel(x + col, y + row, color);
        }
    }
}

/*
 * Draws a single line of text at the supplied framebuffer coordinates.
 */
void gfx_draw_text(uint32_t x, uint32_t y, const char *text, uint32_t foreground, uint32_t background)
{
    if (text == NULL) {
        return;
    }

    while (*text != '\0' && x < gfx_get_width()) {
        gfx_draw_glyph(*text++, x, y, foreground, background);

        x += GLYPH_WIDTH;
    }
}