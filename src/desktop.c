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
#include <desktop.h>
#include <bmp.h>
#include <limine.h>
#include <terminal/terminal.h>
#include <kernel.h>
#include <str.h>
#include <stdbool.h>

extern struct limine_module_request file_request;
extern struct limine_framebuffer *frame_buffer;

static bmp_t logo;
static bool logo_loaded = false;

/*
 * Locates and validates the desktop logo supplied by the bootloader.
 */
void desktop_init(void)
{
    if (file_request.response == NULL) {
        return;
    }

    for (uint64_t i = 0; i < file_request.response->module_count; i++) {
        struct limine_file *module = file_request.response->modules[i];

        klogf("Boot module: %s (%lu bytes)\n", module->path, module->size);

        if (strcmp(module->path, "/Logo.bmp") == 0) {
            logo_loaded = bmp_load(&logo, module->address, module->size);
            break;
        }
    }

    if (logo_loaded) {
        klogf("Desktop logo: %dx%d, %d BPP\n", logo.width, logo.height, logo.bpp);
    } else {
        klogf("Desktop logo: Failed to load Logo.bmp\n");
    }
}

/*
 * Displays the desktop background and centred logo.
 */
void desktop_show(void)
{
    if (frame_buffer == NULL || frame_buffer->bpp != 32) {
        return;
    }

    for (uint64_t y = 0; y < frame_buffer->height; y++) {
        uint32_t *row = (void *)((uint8_t *)frame_buffer->address + y * frame_buffer->pitch);

        for (uint64_t x = 0; x < frame_buffer->width; x++) {
            row[x] = 0x000000;
        }
    }

    if (logo_loaded) {
        int64_t x = ((int64_t)frame_buffer->width - logo.width) / 2;
        int64_t y = ((int64_t)frame_buffer->height - logo.height) / 2;

        bmp_draw(&logo, x, y);
    }
}