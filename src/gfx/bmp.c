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
#include <gfx/bmp.h>
#include <limine.h>

extern struct limine_framebuffer *frame_buffer;

typedef struct {
    uint16_t signature;
    uint32_t file_size;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t pixel_offset;
} __attribute__((packed)) bmp_file_header_t;

typedef struct {
    uint32_t header_size;
    int32_t width;
    int32_t height;
    uint16_t planes;
    uint16_t bpp;
    uint32_t compression;
    uint32_t image_size;
    int32_t x_pixels_per_meter;
    int32_t y_pixels_per_meter;
    uint32_t colors_used;
    uint32_t colors_important;
} __attribute__((packed)) bmp_info_header_t;

/*
 * Validates an uncompressed BMP image and initialises its pixel metadata.
 * Pixel data remains in the original source buffer.
 */
bool bmp_load(bmp_t *bitmap, const void *data, size_t size)
{
    if (bitmap == NULL || data == NULL) {
        return false;
    }

    if (size < sizeof(bmp_file_header_t) + sizeof(bmp_info_header_t)) {
        return false;
    }

    const bmp_file_header_t *file = data;
    const bmp_info_header_t *info = (const void *)((const uint8_t *)data + sizeof(*file));

    if (file->signature != 0x4D42 || info->header_size < sizeof(*info)) {
        return false;
    }

    if (info->width <= 0 || info->height == 0 || info->height == INT32_MIN) {
        return false;
    }

    if (info->planes != 1 || info->compression != 0) {
        return false;
    }

    if (info->bpp != 24 && info->bpp != 32) {
        return false;
    }

    uint32_t width = info->width;
    uint32_t height = info->height < 0 ? -info->height : info->height;
    uint64_t pitch = ((uint64_t)width * (info->bpp / 8) + 3) & ~3ULL;

    if (file->pixel_offset < sizeof(*file) + (uint64_t)info->header_size) {
        return false;
    }

    if (file->pixel_offset > size || height > (size - file->pixel_offset) / pitch) {
        return false;
    }

    bitmap->pixels = (const uint8_t *)data + file->pixel_offset;
    bitmap->width = width;
    bitmap->height = height;
    bitmap->pitch = pitch;
    bitmap->bpp = info->bpp;
    bitmap->top_down = info->height < 0;

    return true;
}

/*
 * Draws a bitmap at the specified framebuffer coordinates.
 * Pixels outside the visible framebuffer are clipped.
 */
void bmp_draw(const bmp_t *bitmap, int64_t x, int64_t y)
{
    if (bitmap == NULL || bitmap->pixels == NULL || frame_buffer == NULL) {
        return;
    }

    if (frame_buffer->bpp != 32) {
        return;
    }

    uint32_t bytes_per_pixel = bitmap->bpp / 8;

    for (uint32_t row = 0; row < bitmap->height; row++) {
        int64_t dest_y = y + row;

        if (dest_y < 0 || (uint64_t)dest_y >= frame_buffer->height) {
            continue;
        }

        uint32_t source_y = bitmap->top_down ? row : bitmap->height - row - 1;
        const uint8_t *source = bitmap->pixels + source_y * bitmap->pitch;
        uint32_t *dest = (void *)((uint8_t *)frame_buffer->address + dest_y * frame_buffer->pitch);

        for (uint32_t col = 0; col < bitmap->width; col++) {
            int64_t dest_x = x + col;

            if (dest_x < 0 || (uint64_t)dest_x >= frame_buffer->width) {
                continue;
            }

            const uint8_t *pixel = source + (uint64_t)col * bytes_per_pixel;

            uint32_t color = ((uint32_t)pixel[2] << 16) |
                             ((uint32_t)pixel[1] << 8) |
                             pixel[0];

            dest[dest_x] = color;
        }
    }
}