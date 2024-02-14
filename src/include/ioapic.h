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
#ifndef _BLOREOS_IOAPIC_H
#define _BLOREOS_IOAPIC_H

#include <stdint.h>
#include <acpi.h>
#include <stdbool.h>

#define IOAPICID          0x00
#define IOAPICVER         0x01
#define IOAPICARB         0x02
#define IOAPICREDTBL(n)   (0x10 + 2 * n) // lower-32bits (add +1 for upper 32-bits)

void ioapic_write(struct ioapic *pApic, const uint8_t offset, const uint32_t val);
uint32_t ioapic_read(struct ioapic *pApic, const uint8_t offset);

void ioapic_redirect_irq(uint32_t lapic_id, uint8_t vector, uint8_t irq, bool status);

#endif