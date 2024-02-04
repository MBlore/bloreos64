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
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include <limine.h>
#include <serial.h>
#include <str.h>
#include <cpu.h>
#include <mem.h>
#include <gdt.h>

// Set the base revision to 1, this is recommended as this is the latest
// base revision described by the Limine boot protocol specification.
// See specification for further info.

LIMINE_BASE_REVISION(1)

// The Limine requests can be placed anywhere, but it is important that
// the compiler does not optimise them away, so, in C, they should
// NOT be made "static".

struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0};

// Halt and catch fire function.
static void hcf(void)
{
    asm("cli");
    for (;;) {
        asm("hlt");
    }
}

static inline void put_pixel(struct limine_framebuffer *framebuffer, int x, int y, uint32_t color)
{
    uint32_t *fb_ptr = framebuffer->address;
    int index = (y * (framebuffer->pitch / 4)) + x;
    fb_ptr[index] = color;
}

void kernel_main(void)
{
    // Ensure the bootloader actually understands our base revision (see spec).
    if (LIMINE_BASE_REVISION_SUPPORTED == false) {
        hcf();
    }

    // Fetch the first framebuffer.
    struct limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[0];

    // Note: we assume the framebuffer model is RGB with 32-bit pixels.
    /*
    for (size_t i = 0; i < 100; i++) {
        uint32_t *fb_ptr = framebuffer->address;
        fb_ptr[i * (framebuffer->pitch / 4) + i] = 0xffffff;
    }
    */

    uint32_t *fb_ptr = framebuffer->address;

    int result = init_serial(PORT_COM1);

    if (result == 0) {
        // Success
        for (size_t x = 0; x < framebuffer->width; x++) {
            for (size_t y = 0; y < framebuffer->height; y++) {
                put_pixel(framebuffer, x, y, 0x00ff00);
            }
        }
    }
    else {
        // Failed :(
        for (size_t i = 0; i < framebuffer->height; i++) {
            fb_ptr[i * (framebuffer->pitch / 4) + (framebuffer->width / framebuffer->height * i)] = 0xff0000;
        }
    }

    kprintf("BloreOS Alpha\n");
    kprintf("Width: %d, Height: %d\n", framebuffer->width, framebuffer->height);

    if (is_paging_enabled()) {
        kprintf("Paging enabled.\n");
    } else {
        kprintf("Paging disabled.\n");
    }

    kmem_init();
    kprintf("PMM Available Pages: %lu\n", num_pages_available);

    void* ptr = kalloc(10);
    kprintf("PMM Available Pages: %lu\n", num_pages_available);

    void* ptr2 = kalloc(5000);
    kprintf("PMM Available Pages: %lu\n", num_pages_available);

    kprintf("Address 1: 0x%X\n", ptr);
    kprintf("Address 2: 0x%X\n", ptr2);

    init_gdt();
    kprintf("GDT initialized.\n");

    hcf();
}