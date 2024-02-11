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
#ifndef _BLOREOS_CPUID_H
#define _BLOREOS_CPUID_H

#include <stdint.h>

static inline void get_cpu_vendor(char *buffer)
{
    uint32_t ebx, edx, ecx;

    asm volatile (
        "cpuid"
        : "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(0)
        :
        );

    *((uint32_t*)buffer) = ebx;
    *((uint32_t*)(buffer + 4)) = edx;
    *((uint32_t*)(buffer + 8)) = ecx;
}

static inline void get_cpu_brand(char *brandString) {
    uint32_t eax, ebx, ecx, edx;
    char *ptr = brandString;

    for (int i = 0; i < 3; i++) {
        asm volatile(
            "cpuid"
            : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
            : "a"(0x80000002 + i)
        );

        *((uint32_t*)ptr) = eax;
        *((uint32_t*)(ptr + 4)) = ebx;
        *((uint32_t*)(ptr + 8)) = ecx;
        *((uint32_t*)(ptr + 12)) = edx;
        ptr += 16;
    }
}

static inline void get_cpu_topology(uint32_t *logicalProcessorsPerCore, uint32_t *totalLogicalProcessors) {
    uint32_t eax, ebx, ecx, edx;
    uint32_t levelType = 0;
    *logicalProcessorsPerCore = 0;
    *totalLogicalProcessors = 0;

    // Initial call to CPUID with EAX=0xB and ECX=0 to get the number of logical processors
    uint32_t level = 0;
    asm volatile(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(0xB), "c"(level)
    );

    // ebx[15:0] contains the number of logical processors at this level
    if (ebx & 0xFFFF) {
        *totalLogicalProcessors = ebx & 0xFFFF;
    }

    // Loop through the levels to find the core level (level type == 2)
    while (ecx & 0xFF00) {
        levelType = (ecx >> 8) & 0xFF; // Extract the level type
        if (levelType == 2) { // Check if this is the core level
            *logicalProcessorsPerCore = ebx & 0xFFFF; // Number of logical processors per core
            break;
        }

        level++;
        asm volatile(
            "cpuid"
            : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
            : "a"(0xB), "c"(level)
        );
    }
}

static inline void get_cpu_frequency(uint32_t *baseFrequencyMHz, uint32_t *maxFrequencyMHz, uint32_t *busFrequencyMHz) {
    uint32_t eax, ebx, ecx, edx;

    asm volatile(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(0x16)
    );

    *baseFrequencyMHz = eax & 0xFFFF;
    *maxFrequencyMHz = ebx & 0xFFFF;
    *busFrequencyMHz = ecx & 0xFFFF;
}

#endif