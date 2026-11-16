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
#include <stddef.h>
#include <block.h>
#include <str.h>

static block_device_t *devices[BLOCK_MAX_DEVICES];
static uint32_t device_count = 0;

bool block_register(block_device_t *device)
{
    // Validate the supplied device.
    if (device == NULL ||
        device->name[0] == '\0' ||
        device->block_size == 0 ||
        device->block_count == 0 ||
        device->read == NULL) {
        return false;
    }

    // Ensure there is room for another device.
    if (device_count >= BLOCK_MAX_DEVICES) {
        return false;
    }

    // Ensure the device name is null-terminated.
    if (device->name[BLOCK_DEVICE_NAME_LENGTH - 1] != '\0') {
        return false;
    }

    // Reject duplicate device names.
    if (block_find(device->name) != NULL) {
        return false;
    }

    devices[device_count++] = device;

    return true;
}

block_device_t *block_find(const char *name)
{
    if (name == NULL) {
        return NULL;
    }

    // Search the registered block devices.
    for (uint32_t i = 0; i < device_count; i++) {
        if (strcmp(devices[i]->name, name) == 0) {
            return devices[i];
        }
    }

    return NULL;
}

bool block_read(block_device_t *device,
                uint64_t lba,
                uint32_t count,
                void *buffer)
{
    // Validate the read request.
    if (device == NULL ||
        device->read == NULL ||
        buffer == NULL ||
        count == 0) {
        return false;
    }

    // Ensure the requested blocks are within the device.
    if (lba >= device->block_count ||
        count > device->block_count - lba) {
        return false;
    }

    // Delegate the request to the underlying driver.
    return device->read(device->context, lba, count, buffer);
}

bool block_write(block_device_t *device,
                 uint64_t lba,
                 uint32_t count,
                 const void *buffer)
{
    // Validate the write request.
    if (device == NULL ||
        device->write == NULL ||
        buffer == NULL ||
        count == 0) {
        return false;
    }

    // Ensure the requested blocks are within the device.
    if (lba >= device->block_count ||
        count > device->block_count - lba) {
        return false;
    }

    // Delegate the request to the underlying driver.
    return device->write(device->context, lba, count, buffer);
}

// Return the number of registered block devices.
uint32_t block_get_count(void)
{
    return device_count;
}

// Retrieve a registered block device by its index.
block_device_t *block_get_device(uint32_t index)
{
    if (index >= device_count) {
        return NULL;
    }

    return devices[index];
}