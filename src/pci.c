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

struct pci_device *pci_devices[32];
uint8_t pci_device_cnt = 0;

/*
 * Gets the device string represenation of the specified class and sub-class codes.
*/
const char* _get_device_str(uint8_t class_code, uint8_t sub_class_code)
{
    // https://wiki.osdev.org/PCI#Class_Codes
    switch (class_code) {
        case 0x1:
            switch (sub_class_code) {
                case 0x6:
                    return "Mass Storage Controller - Serial ATA Controller";
                case 0x8:
                    return "Mass Storage Controller - Non-Volatile Memory Controller";
                default:
                    kprintf("PCI: %d / %d\n", class_code, sub_class_code);
                    return "Unknown Device";
            }
        case 0x2:
            switch (sub_class_code) {
                case 0x0:
                    return "Network Controller - Ethernet Controller";
                default:
                    kprintf("PCI: %d / %d\n", class_code, sub_class_code);
                    return "Unknown Device";
            }
        case 0x3:
            switch (sub_class_code) {
                case 0x0:
                    return "Display Controller - VGA Compatible Controller";
                default:
                    kprintf("PCI: %d / %d\n", class_code, sub_class_code);
                    return "Unknown Device";
            }
        case 0x6:
            switch (sub_class_code) {
                case 0x0:
                    return "Bridge - Host Bridge";
                case 0x1:
                    return "Bridge - ISA Bridge";
                default:
                    kprintf("PCI: %d / %d\n", class_code, sub_class_code);
                    return "Unknown Device";
            }
        case 0xC:
            switch (sub_class_code) {
                case 0x5:
                    return "Serial Bus Controller - SMBus Controller";
                default:
                    kprintf("PCI: %d / %d\n", class_code, sub_class_code);
                    return "Unknown Device";
            }
    }

    kprintf("PCI: %d / %d\n", class_code, sub_class_code);
    return "Unknown Device";
}

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
void _check_function(uint8_t bus, uint8_t device, uint8_t function, uint16_t header_type)
{
    uint8_t classcode = _pci_mm_read_class_code(bus, device, function);
    uint8_t subclass = _pci_mm_read_subclass_code(bus, device, function);
    uint8_t progif = _pci_mm_read_prog_if(bus, device, function);

    // Create the new device and store it in the device list.
    struct pci_device *dev = kalloc(sizeof(struct pci_device));
    pci_devices[pci_device_cnt++] = dev;
    dev->class_code = classcode;
    dev->sub_class_code = subclass;
    dev->prog_if = progif;
    dev->description = _get_device_str(classcode, subclass);
    dev->bus = bus;
    dev->function = function;
    dev->device = device;
    dev->header_type = header_type;
    dev->address = mcfg->mmio_base + vmm_higher_half_offset + ((bus - mcfg->start) << 20 | device << 15 | function << 12);
    dev->bar0_address = _pci_mm_read(bus, device, function, 0x10, 4) + vmm_higher_half_offset;
    dev->bar1_address = _pci_mm_read(bus, device, function, 0x14, 4) + vmm_higher_half_offset;

    kprintf("PCI: %s\n", dev->description);
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

    uint16_t headerType = _pci_mm_read_header_type(bus, device, function);

    _check_function(bus, device, function, headerType);

    if ((headerType & 0x80) != 0) {
        // It's a multi-function device, so check remaining functions.
        for (function = 1; function < 8; function++) {
            if (_pci_mm_read_vendor_id(bus, device, function) != 0xFFFF) {
                _check_function(bus, device, function, headerType);
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
    kprintf("PCI: Found %d PCI devices.\n", pci_device_cnt);
    kprintf("PCI: Initialized.\n");
}

/*
 * Finds a device by its class and sub-class code.
*/
struct pci_device* pci_find_device(uint8_t class, uint8_t subclass)
{
    for (int i = 0; i < pci_device_cnt; i++) {
        struct pci_device *dev = pci_devices[i];
        if (dev->class_code == class && dev->sub_class_code == subclass) {
            return dev;
        }
    }

    return NULL;
}