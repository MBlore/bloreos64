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
#include <desktop/desktop.h>
#include <gfx/gfx.h>
#include <gfx/bmp.h>
#include <limine.h>
#include <terminal/terminal.h>
#include <kernel.h>
#include <str.h>
#include <stdbool.h>
#include <time.h>

#define DESKTOP_TASKBAR_HEIGHT       36
#define DESKTOP_TITLEBAR_HEIGHT      28
#define DESKTOP_WINDOW_PADDING       8
#define DESKTOP_WINDOW_BORDER        1

#define DESKTOP_TASKBAR_COLOR        0x17212C
#define DESKTOP_TITLEBAR_COLOR       0x203348
#define DESKTOP_BORDER_COLOR         0x536475
#define DESKTOP_START_COLOR          0x244D68
#define DESKTOP_TASK_COLOR           0x293B4D
#define DESKTOP_TEXT_COLOR           0xFFFFFF
#define DESKTOP_TERMINAL_COLOR       0x000000

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

        if (strcmp(module->path, "/Logo.bmp") == 0) {
            logo_loaded = bmp_load(&logo, module->address, module->size);
            break;
        }
    }
}

/*
 * Draws the black background and centred BloreOS logo.
 */
static void _draw_background(void)
{
    gfx_clear(0x000000);

    if (logo_loaded) {
        int64_t x = ((int64_t)gfx_get_width() - logo.width) / 2;
        int64_t y = ((int64_t)gfx_get_height() - logo.height) / 2;

        bmp_draw(&logo, x, y);
    }
}

/*
 * Displays the splash screen during kernel initialisation.
 */
void splash_show(void)
{
    _draw_background();
}

/*
 * Draws the current system time on the right side of the taskbar.
 */
static void _draw_clock(void)
{
    uint32_t width = gfx_get_width();
    uint32_t height = gfx_get_height();
    uint32_t glyph_width = gfx_get_glyph_width();
    uint32_t glyph_height = gfx_get_glyph_height();

    if (width < (5 * glyph_width) + 12 || height < DESKTOP_TASKBAR_HEIGHT ||
        glyph_height > DESKTOP_TASKBAR_HEIGHT) {
        return;
    }

    char text[] = "--:--";
    rtc_datetime_t now;

    if (time_get_datetime(&now)) {
        text[0] = '0' + now.hour / 10;
        text[1] = '0' + now.hour % 10;
        text[3] = '0' + now.minute / 10;
        text[4] = '0' + now.minute % 10;
    }

    uint32_t x = width - (5 * glyph_width) - 12;
    uint32_t y = height - DESKTOP_TASKBAR_HEIGHT;
    y += (DESKTOP_TASKBAR_HEIGHT - glyph_height) / 2;

    gfx_draw_text(x, y, text, DESKTOP_TEXT_COLOR, DESKTOP_TASKBAR_COLOR);
}

/*
 * Draws the desktop taskbar, Start button and active terminal entry.
 */
static void _draw_taskbar(void)
{
    uint32_t width = gfx_get_width();
    uint32_t height = gfx_get_height();
    uint32_t glyph_height = gfx_get_glyph_height();

    if (height < DESKTOP_TASKBAR_HEIGHT || glyph_height > 26) {
        return;
    }

    uint32_t y = height - DESKTOP_TASKBAR_HEIGHT;
    uint32_t text_y = y + (DESKTOP_TASKBAR_HEIGHT - glyph_height) / 2;

    gfx_fill_rect(0, y, width, DESKTOP_TASKBAR_HEIGHT, DESKTOP_TASKBAR_COLOR);
    gfx_fill_rect(0, y, width, 1, DESKTOP_BORDER_COLOR);

    // Start button.
    gfx_fill_rect(5, y + 5, 80, 26, DESKTOP_START_COLOR);
    gfx_draw_rect(5, y + 5, 80, 26, DESKTOP_BORDER_COLOR);
    gfx_draw_text(22, text_y, "Start", DESKTOP_TEXT_COLOR, DESKTOP_START_COLOR);

    // Active terminal task.
    gfx_fill_rect(92, y + 5, 140, 26, DESKTOP_TASK_COLOR);
    gfx_draw_rect(92, y + 5, 140, 26, DESKTOP_BORDER_COLOR);
    gfx_draw_text(104, text_y, "Terminal", DESKTOP_TEXT_COLOR, DESKTOP_TASK_COLOR);

    _draw_clock();
}

/*
 * Draws the terminal window decorations and configures its internal viewport.
 */
static void _draw_terminal_window(void)
{
    uint32_t screen_width = gfx_get_width();
    uint32_t screen_height = gfx_get_height();

    if (screen_height <= DESKTOP_TASKBAR_HEIGHT) {
        return;
    }

    uint32_t x = 24;
    uint32_t y = 24;
    uint32_t width = screen_width * 60 / 100;
    uint32_t height = (screen_height - DESKTOP_TASKBAR_HEIGHT) * 75 / 100;

    uint32_t inset = DESKTOP_WINDOW_BORDER + DESKTOP_WINDOW_PADDING;
    uint32_t top_inset = DESKTOP_TITLEBAR_HEIGHT + DESKTOP_WINDOW_PADDING;

    if (width < (2 * inset) + gfx_get_glyph_width() ||
        height < top_inset + inset + gfx_get_glyph_height()) {
        return;
    }

    if (x + width > screen_width || y + height > screen_height - DESKTOP_TASKBAR_HEIGHT) {
        return;
    }

    // Window background.
    gfx_fill_rect(x, y, width, height, DESKTOP_TERMINAL_COLOR);

    // Title bar.
    gfx_fill_rect(x, y, width, DESKTOP_TITLEBAR_HEIGHT, DESKTOP_TITLEBAR_COLOR);

    uint32_t title_y = y + (DESKTOP_TITLEBAR_HEIGHT - gfx_get_glyph_height()) / 2;

    gfx_draw_text(x + 10, title_y, "Terminal", DESKTOP_TEXT_COLOR, DESKTOP_TITLEBAR_COLOR);

    // Outer window border.
    gfx_draw_rect(x, y, width, height, DESKTOP_BORDER_COLOR);

    // Separator between the title bar and terminal content.
    gfx_fill_rect(x, y + DESKTOP_TITLEBAR_HEIGHT - 1, width, 1, DESKTOP_BORDER_COLOR);

    // Configure the terminal's drawable content area.
    term_set_viewport(
        x + inset,
        y + top_inset,
        width - (2 * inset),
        height - top_inset - inset
    );
}

/*
 * Displays the desktop after kernel initialisation.
 */
void desktop_show(void)
{
    _draw_background();
    _draw_taskbar();
    _draw_terminal_window();
}