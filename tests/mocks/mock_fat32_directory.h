
#ifndef TEST_MOCK_FAT32_DIRECTORY_H
#define TEST_MOCK_FAT32_DIRECTORY_H

#include <stdbool.h>
#include <stdint.h>
#include <partition.h>
#include <fat32.h>

#define TEST_FIRST_CLUSTER FAT32_FIRST_DATA_CLUSTER
#define TEST_CLUSTER_COUNT 3
#define TEST_ENTRIES_PER_CLUSTER 4
#define TEST_CLUSTER_BYTES \
    (TEST_ENTRIES_PER_CLUSTER * FAT32_DIRECTORY_ENTRY_SIZE)

void mock_fat32_reset(void);
void mock_fat32_set_marker(uint32_t cluster, uint32_t index, uint8_t marker);
void mock_fat32_link(uint32_t cluster, uint32_t next_cluster);
void mock_fat32_fail_read(uint32_t cluster);
void mock_fat32_fail_extend(void);
uint32_t mock_fat32_extension_count(void);
void mock_fat32_fail_release(bool fail);

/*
 * Controls and inspects simulated directory writes.
 */
void mock_fat32_fail_write(uint32_t write_number);
uint32_t mock_fat32_write_count(void);

bool mock_fat32_get_entry(uint32_t cluster, uint32_t index,
                          fat32_directory_entry_t *entry);

#endif
