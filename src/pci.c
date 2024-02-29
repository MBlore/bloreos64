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
#include <pci.h>
#include <str.h>
#include <stdint.h>
#include <acpi.h>
#include <mem.h>

/*
 * Read from the PCI Configuration MM space.
*/
uint32_t _pci_mm_read(uint8_t bus, uint8_t device, uint8_t function, uint32_t offset, uint8_t size)
{
    uint64_t addr = mcfg->mmio_base + vmm_higher_half_offset + offset +
        ((bus - mcfg->start) << 20 | device << 15 | function << 12);

    switch(size) {
        case 1:
            return *(uint8_t*)addr;
        case 2:
            return *(uint16_t*)addr;
        case 4:
            return *(uint32_t*)addr;
    }

    return 0;
}

/*
 * Reads from PCI MM config for a devices vendor ID.
*/
uint16_t _pci_mm_read_vendor_id(uint8_t bus, uint8_t device, uint8_t function)
{
    uint16_t reg0 = _pci_mm_read(bus, device, function, 0x0, 2);
    return reg0;
}

/*
 * Reads from PCI MM config for a devices header type.
*/
uint8_t _pci_mm_read_header_type(uint8_t bus, uint8_t device, uint8_t function)
{   
    uint32_t reg3 = _pci_mm_read(bus, device, function, 0xC, 4);
    return (uint8_t)(reg3 >> 16);
}

/*
 * Reads from PCI MM config for a devices class code.
*/
uint8_t _pci_mm_read_class_code(uint8_t bus, uint8_t device, uint8_t function)
{
    uint32_t reg2 = _pci_mm_read(bus, device, function, 0x8, 4);
    uint8_t classcode = (uint8_t)(reg2 >> 24);

    return classcode;
}

/*
 * Reads from PCI MM config for a devices sub-class code.
*/
uint8_t _pci_mm_read_subclass_code(uint8_t bus, uint8_t device, uint8_t function)
{
    uint32_t reg2 = _pci_mm_read(bus, device, function, 0x8, 4);
    uint8_t subclass = (uint8_t)(reg2 >> 16);
    return subclass;
}

/*
 * Reads from PCI MM config for a devices prog IF.
*/
uint8_t _pci_mm_read_prog_if(uint8_t bus, uint8_t device, uint8_t function)
{
    uint32_t reg2 = _pci_mm_read(bus, device, function, 0x8, 4);
    uint8_t progif = (uint8_t)(reg2 >> 8);
    return progif;
}

/*
 * Inspects a found device at the specific PCI bus, device, function location.
*/
void _check_function(uint8_t bus, uint8_t device, uint8_t function)
{
    uint8_t classcode = _pci_mm_read_class_code(bus, device, function);
    uint8_t subclass = _pci_mm_read_subclass_code(bus, device, function);
    uint8_t progif = _pci_mm_read_prog_if(bus, device, function);

    kprintf("PCI: Found device class %d/%d/%d at %d %d %d\n",
        classcode, subclass, progif, bus, device, function);
}

/*
 * Checks if a device is present at the specified bus and device. 
*/
void _check_device(uint8_t bus, uint8_t device)
{
    uint8_t function = 0;

    uint16_t vendorID = _pci_mm_read_vendor_id(bus, device, function);
    if (vendorID == 0xFFFF) {
        return;
    }

    _check_function(bus, device, function);

    uint16_t headerType = _pci_mm_read_header_type(bus, device, function);
    if ((headerType & 0x80) != 0) {
        // It's a multi-function device, so check remaining functions.
        for (function = 1; function < 8; function++) {
            if (_pci_mm_read_vendor_id(bus, device, function) != 0xFFFF) {
                _check_function(bus, device, function);
            }
        }
    }
}

/*
 * Scans every device ID on every bus ID looking for present devices.
*/
void _scan_all_buses()
{
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            _check_device(bus, device);
        }
    }
}

void pci_init()
{
    _scan_all_buses();
    kprintf("PCI Initialized.\n");
}
