
#include "mock_fat32_directory.h"
#include <stdlib.h>
#include <string.h>

#define TEST_MAX_CLUSTER \
    (TEST_FIRST_CLUSTER + TEST_CLUSTER_COUNT - 1)

static uint8_t cluster_data[TEST_CLUSTER_COUNT][TEST_CLUSTER_BYTES];
static uint32_t next_cluster[TEST_CLUSTER_COUNT];
static bool read_failure[TEST_CLUSTER_COUNT];
static bool extend_failure;
static uint32_t extension_count;
static uint32_t write_count;
static uint32_t failing_write_number;

static bool valid_cluster(uint32_t cluster)
{
    return cluster >= TEST_FIRST_CLUSTER && cluster <= TEST_MAX_CLUSTER;
}

static uint32_t cluster_index(uint32_t cluster)
{
    return cluster - TEST_FIRST_CLUSTER;
}

void mock_fat32_reset(void)
{
    memset(cluster_data, 0, sizeof(cluster_data));
    memset(next_cluster, 0, sizeof(next_cluster));
    memset(read_failure, 0, sizeof(read_failure));

    extend_failure = false;
    extension_count = 0;
    write_count = 0;
    failing_write_number = 0;

    // Initially, cluster 2 is the complete directory chain.
    next_cluster[0] = FAT32_CLUSTER_EOC;
}

void mock_fat32_set_marker(uint32_t cluster, uint32_t index, uint8_t marker)
{
    if (!valid_cluster(cluster) || index >= TEST_ENTRIES_PER_CLUSTER) {
        return;
    }

    uint32_t offset = index * FAT32_DIRECTORY_ENTRY_SIZE;
    cluster_data[cluster_index(cluster)][offset] = marker;
}

void mock_fat32_link(uint32_t cluster, uint32_t next)
{
    if (valid_cluster(cluster)) {
        next_cluster[cluster_index(cluster)] = next;
    }
}

void mock_fat32_fail_read(uint32_t cluster)
{
    if (valid_cluster(cluster)) {
        read_failure[cluster_index(cluster)] = true;
    }
}

void mock_fat32_fail_extend(void)
{
    extend_failure = true;
}

uint32_t mock_fat32_extension_count(void)
{
    return extension_count;
}

void *kalloc(size_t size)
{
    return malloc(size);
}

void kfree(void *ptr)
{
    free(ptr);
}

bool fat32_read_cluster(const fat32_volume_t *volume, uint32_t cluster, void *buffer)
{
    if (volume == NULL || buffer == NULL || !valid_cluster(cluster) ||
        read_failure[cluster_index(cluster)] ||
        volume->bytes_per_cluster != TEST_CLUSTER_BYTES) {
        return false;
    }

    memcpy(buffer, cluster_data[cluster_index(cluster)], TEST_CLUSTER_BYTES);
    return true;
}

fat32_chain_status_t fat32_next_cluster(const fat32_volume_t *volume,
                                       uint32_t cluster, uint32_t *next)
{
    if (volume == NULL || next == NULL || !valid_cluster(cluster)) {
        return FAT32_CHAIN_ERROR;
    }

    uint32_t value = next_cluster[cluster_index(cluster)];

    if (value == FAT32_CLUSTER_EOC) {
        return FAT32_CHAIN_END;
    }

    if (!valid_cluster(value)) {
        return FAT32_CHAIN_ERROR;
    }

    *next = value;
    return FAT32_CHAIN_NEXT;
}

bool fat32_extend_chain(const fat32_volume_t *volume, uint32_t tail,
                        uint32_t *cluster)
{
    if (volume == NULL || cluster == NULL || !valid_cluster(tail) ||
        extend_failure ||
        next_cluster[cluster_index(tail)] != FAT32_CLUSTER_EOC) {
        return false;
    }

    // Find a cluster that is not already part of the directory.
    for (uint32_t candidate = TEST_FIRST_CLUSTER;
         candidate <= TEST_MAX_CLUSTER; candidate++) {
        if (next_cluster[cluster_index(candidate)] != 0) {
            continue;
        }

        next_cluster[cluster_index(tail)] = candidate;
        next_cluster[cluster_index(candidate)] = FAT32_CLUSTER_EOC;

        memset(cluster_data[cluster_index(candidate)], 0, TEST_CLUSTER_BYTES);

        *cluster = candidate;
        extension_count++;
        return true;
    }

    return false;
}


/*
 * Configures a simulated failure on a specific write attempt.
 *
 * Write numbers are one-based. Zero disables failures.
 */
void mock_fat32_fail_write(uint32_t write_number)
{
    failing_write_number = write_number;
}

uint32_t mock_fat32_write_count(void)
{
    return write_count;
}

/*
 * Reads a directory entry directly from the mock's memory.
 */
bool mock_fat32_get_entry(uint32_t cluster, uint32_t index,
                          fat32_directory_entry_t *entry)
{
    if (!valid_cluster(cluster) ||
        index >= TEST_ENTRIES_PER_CLUSTER ||
        entry == NULL) {
        return false;
    }

    uint32_t offset = index * FAT32_DIRECTORY_ENTRY_SIZE;

    memcpy(entry, cluster_data[cluster_index(cluster)] + offset,
           sizeof(*entry));

    return true;
}

/*
 * Simulates writing an entire FAT32 cluster.
 *
 * The write counter allows tests to fail a particular write
 * attempt and verify which directory entries were committed.
 */
bool fat32_write_cluster(const fat32_volume_t *volume,
                         uint32_t cluster,
                         const void *buffer)
{
    if (volume == NULL || buffer == NULL ||
        !valid_cluster(cluster) ||
        volume->bytes_per_cluster != TEST_CLUSTER_BYTES) {
        return false;
    }

    write_count++;

    if (failing_write_number != 0 &&
        write_count == failing_write_number) {
        return false;
    }

    memcpy(cluster_data[cluster_index(cluster)], buffer,
           TEST_CLUSTER_BYTES);

    return true;
}

/*
 * Simulates releasing a file's cluster chain.
 *
 * Directory-only tests do not need to modify the FAT. A separate
 * integration test will verify actual cluster-chain release.
 */
bool fat32_release_chain(const fat32_volume_t *volume, uint32_t first_cluster)
{
    (void)volume;
    (void)first_cluster;

    return true;
}