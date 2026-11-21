#include <fat32.h>

/*
 * Simulates an unavailable system clock for FAT32 tests.
 * The timestamp functions leave directory entries unchanged.
 */
bool fat32_timestamp_now(fat32_directory_entry_t *entry, bool creation)
{
    (void)entry;
    (void)creation;
    return false;
}

bool fat32_timestamp_access(fat32_directory_entry_t *entry)
{
    (void)entry;
    return false;
}