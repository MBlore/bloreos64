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
#ifndef _BLOREOS_FAT32_H
#define _BLOREOS_FAT32_H

#include <stdint.h>
#include <stddef.h>

#define FAT32_BOOT_SECTOR_SIZE 512
#define FAT32_BOOT_SIGNATURE 0xAA55

#define FAT32_MAX_SECTORS_PER_CLUSTER 128
#define FAT32_FILESYSTEM_VERSION 0
#define FAT32_FIRST_DATA_CLUSTER 2
#define FAT32_MIN_CLUSTER_COUNT 65525
#define FAT32_MAX_CLUSTER_NUMBER 0x0FFFFFEF
#define FAT32_ENTRY_SIZE 4

#define FAT32_CLUSTER_MASK 0x0FFFFFFF
#define FAT32_CLUSTER_FREE 0x00000000
#define FAT32_CLUSTER_BAD 0x0FFFFFF7
#define FAT32_CLUSTER_EOC_MIN 0x0FFFFFF8

#define FAT32_MIRRORING_DISABLED 0x0080
#define FAT32_ACTIVE_FAT_MASK 0x000F

#define FAT32_DIRECTORY_ENTRY_SIZE 32
#define FAT32_SHORT_NAME_LENGTH 11
#define FAT32_DIR_ENTRY_END 0x00
#define FAT32_DIR_ENTRY_DELETED 0xE5
#define FAT32_DIR_ENTRY_ESCAPED 0x05

#define FAT32_ATTR_READ_ONLY 0x01
#define FAT32_ATTR_HIDDEN 0x02
#define FAT32_ATTR_SYSTEM 0x04
#define FAT32_ATTR_VOLUME_ID 0x08
#define FAT32_ATTR_DIRECTORY 0x10
#define FAT32_ATTR_ARCHIVE 0x20
#define FAT32_ATTR_LONG_NAME 0x0F

#define FAT32_SHORT_NAME_BASE_LENGTH 8
#define FAT32_SHORT_NAME_EXT_LENGTH 3
#define FAT32_SHORT_NAME_BUFFER_SIZE 13

#define FAT32_NT_LOWERCASE_BASE 0x08
#define FAT32_NT_LOWERCASE_EXT 0x10

#define FAT32_LFN_LAST_ENTRY 0x40
#define FAT32_LFN_ORDER_MASK 0x1F
#define FAT32_LFN_MAX_LENGTH 255
#define FAT32_LFN_CHARS_PER_ENTRY 13
#define FAT32_LFN_MAX_ENTRIES 20
#define FAT32_LFN_PADDING 0xFFFF
#define FAT32_LFN_BUFFER_SIZE (FAT32_LFN_MAX_ENTRIES * FAT32_LFN_CHARS_PER_ENTRY)
#define FAT32_LFN_RESERVED_ORDER_MASK 0xA0

#define FAT32_UTF8_NAME_BUFFER_SIZE (FAT32_LFN_MAX_LENGTH * 3 + 1)

#define FAT32_CLUSTER_EOC 0x0FFFFFFF

typedef enum {
    FAT32_CHAIN_ERROR,
    FAT32_CHAIN_NEXT,
    FAT32_CHAIN_END
} fat32_chain_status_t;

typedef enum {
    FAT32_DIR_ERROR,
    FAT32_DIR_NEXT,
    FAT32_DIR_END
} fat32_directory_status_t;

typedef enum {
    FAT32_LOOKUP_ERROR,
    FAT32_LOOKUP_FOUND,
    FAT32_LOOKUP_NOT_FOUND,
    FAT32_LOOKUP_NOT_DIRECTORY
} fat32_lookup_status_t;

/*
 * Defines the reference position used when seeking within a file.
 */
typedef enum {
    FAT32_SEEK_SET = 0,    // Relative to the beginning of the file.
    FAT32_SEEK_CUR = 1,    // Relative to the current file position.
    FAT32_SEEK_END = 2     // Relative to the end of the file.
} fat32_seek_mode_t;

// FAT32 boot sector, including the BIOS Parameter Block.
typedef struct __attribute__((packed)) {
    uint8_t jump_instruction[3];
    uint8_t oem_name[8];

    // BIOS Parameter Block (BPB).
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sector_count;
    uint8_t fat_count;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t media_type;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t head_count;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;

    // FAT32 extended BIOS Parameter Block.
    uint32_t fat_size_32;
    uint16_t extended_flags;
    uint16_t filesystem_version;
    uint32_t root_cluster;
    uint16_t fsinfo_sector;
    uint16_t backup_boot_sector;
    uint8_t reserved[12];
    uint8_t drive_number;
    uint8_t reserved_1;
    uint8_t extended_boot_signature;
    uint32_t volume_id;
    uint8_t volume_label[11];
    uint8_t filesystem_type[8];

    // Bootstrap code and boot sector signature.
    uint8_t boot_code[420];
    uint16_t boot_signature;
} fat32_boot_sector_t;

_Static_assert(
    sizeof(fat32_boot_sector_t) == FAT32_BOOT_SECTOR_SIZE,
    "Invalid FAT32 boot sector structure size"
);

// Represents an initialised FAT32 filesystem volume.
typedef struct {
    partition_t *partition;

    // Filesystem geometry.
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t bytes_per_cluster;
    uint32_t total_sectors;
    uint32_t active_fat_sector;

    // File Allocation Table geometry.
    uint32_t fat_count;
    uint32_t sectors_per_fat;
    uint32_t first_fat_sector;
    bool fat_mirroring;         // Whether writes must update all FAT copies.

    // Data region geometry.
    uint32_t first_data_sector;
    uint32_t cluster_count;
    uint32_t root_cluster;
} fat32_volume_t;

typedef struct __attribute__((packed)) {
    uint8_t name[FAT32_SHORT_NAME_LENGTH];
    uint8_t attributes;
    uint8_t nt_reserved;
    uint8_t creation_time_tenths;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t last_access_date;
    uint16_t first_cluster_high;
    uint16_t modification_time;
    uint16_t modification_date;
    uint16_t first_cluster_low;
    uint32_t file_size;
} fat32_directory_entry_t;

_Static_assert(sizeof(fat32_directory_entry_t) == FAT32_DIRECTORY_ENTRY_SIZE,
               "Invalid FAT32 directory entry structure size");

typedef struct {
    uint16_t name[FAT32_LFN_BUFFER_SIZE];
    uint8_t checksum;
    uint8_t expected_order;
    uint8_t entry_count;
    bool active;
} fat32_lfn_state_t;

typedef struct {
    const fat32_volume_t *volume;
    uint32_t current_cluster;
    uint32_t entry_index;
    uint32_t clusters_visited;
    uint8_t *buffer;
    bool finished;
    fat32_lfn_state_t lfn;
} fat32_directory_iterator_t;

typedef struct __attribute__((packed)) {
    uint8_t order;
    uint16_t name_1[5];
    uint8_t attributes;
    uint8_t type;
    uint8_t checksum;
    uint16_t name_2[6];
    uint16_t first_cluster_low;
    uint16_t name_3[2];
} fat32_long_directory_entry_t;

_Static_assert(sizeof(fat32_long_directory_entry_t) == FAT32_DIRECTORY_ENTRY_SIZE,
               "Invalid FAT32 long directory entry structure size");

/*
 * Represents a resolved directory entry and its location on disk.
 *
 * Synthetic records, such as the root directory, have no physical
 * directory entry and therefore use a directory cluster of zero.
 */
typedef struct {
    fat32_directory_entry_t entry;              // On-disk short directory entry.
    char name[FAT32_UTF8_NAME_BUFFER_SIZE];     // Resolved UTF-8 filename.
    uint32_t directory_cluster;                 // Cluster containing the directory entry.
    uint32_t directory_index;                   // Zero-based entry index within the cluster.
} fat32_directory_record_t;

/*
 * Represents an open file on a FAT32 volume.
 *
 * Maintains the current file position, directory entry location,
 * cluster chain traversal state and cached cluster data.
 *
 * Each open file maintains its own independent state.
 */
typedef struct {
    const fat32_volume_t *volume;    // Volume containing the file.
    uint32_t directory_cluster;      // Cluster containing the directory entry.
    uint32_t directory_index;        // Entry index within the directory cluster.
    uint32_t first_cluster;          // First cluster in the file's FAT chain.
    uint32_t current_cluster;        // Current cluster in the FAT chain.
    uint32_t cluster_index;          // Zero-based logical cluster index.
    uint64_t size;                   // Total file size in bytes.
    uint64_t position;               // Current file position in bytes.
    uint8_t attributes;              // FAT32 directory entry attributes.
    uint8_t *buffer;                 // Allocated cluster cache buffer.
    bool buffer_valid;               // Indicates whether cached data is valid.
} fat32_file_t;

bool fat32_read_boot_sector(partition_t *partition, fat32_boot_sector_t *boot_sector);
bool fat32_validate_boot_sector(partition_t *partition, const fat32_boot_sector_t *boot_sector);
bool fat32_init_volume(partition_t *partition, fat32_volume_t *volume);
bool fat32_cluster_to_lba(const fat32_volume_t *volume, uint32_t cluster, uint64_t *lba);

bool fat32_read_cluster(const fat32_volume_t *volume, uint32_t cluster, void *buffer);
bool fat32_find_free_cluster(const fat32_volume_t *volume, uint32_t *cluster);
bool fat32_write_cluster(const fat32_volume_t *volume, uint32_t cluster, const void *buffer);
bool fat32_allocate_cluster(const fat32_volume_t *volume, uint32_t *cluster);
bool fat32_release_cluster(const fat32_volume_t *volume, uint32_t cluster);

bool fat32_extend_chain(const fat32_volume_t *volume, uint32_t tail, uint32_t *cluster);
bool fat32_release_chain(const fat32_volume_t *volume, uint32_t first_cluster);

bool fat32_read_fat_entry(const fat32_volume_t *volume, uint32_t cluster, uint32_t *entry);
bool fat32_write_fat_entry(const fat32_volume_t *volume, uint32_t cluster, uint32_t entry);

fat32_chain_status_t fat32_next_cluster(const fat32_volume_t *volume, uint32_t cluster,
                                       uint32_t *next_cluster);

uint32_t fat32_get_first_cluster(const fat32_directory_entry_t *entry);
void fat32_format_short_name(const fat32_directory_entry_t *entry, char *name);
bool fat32_make_short_name(const char *filename,
                           uint8_t name[FAT32_SHORT_NAME_LENGTH]);
bool fat32_make_short_alias(const char *filename, uint32_t sequence,
                            uint8_t name[FAT32_SHORT_NAME_LENGTH]);


bool fat32_directory_open(const fat32_volume_t *volume, uint32_t cluster,
                          fat32_directory_iterator_t *iterator);

void fat32_directory_close(fat32_directory_iterator_t *iterator);
bool fat32_directory_read_entry(const fat32_volume_t *volume, uint32_t cluster,
                                uint32_t index, fat32_directory_entry_t *entry);
bool fat32_directory_write_entry(const fat32_volume_t *volume, uint32_t cluster,
                                 uint32_t index, const fat32_directory_entry_t *entry);

fat32_directory_status_t fat32_directory_next(fat32_directory_iterator_t *iterator,
                                             fat32_directory_record_t *record);

bool fat32_directory_find_free_entry(const fat32_volume_t *volume,
                                     uint32_t directory_cluster,
                                     uint32_t *entry_cluster,
                                     uint32_t *entry_index);

bool fat32_directory_find_free_entries(const fat32_volume_t *volume,
                                       uint32_t directory_cluster,
                                       uint32_t required_entries,
                                       uint32_t *entry_cluster,
                                       uint32_t *entry_index);

bool fat32_directory_write_long_name(const fat32_volume_t *volume,
                                     uint32_t entry_cluster,
                                     uint32_t entry_index,
                                     const uint16_t *name,
                                     uint32_t name_length,
                                     const fat32_directory_entry_t *short_entry);

typedef enum {
    FAT32_SHORT_NAME_NOT_FOUND,
    FAT32_SHORT_NAME_FOUND,
    FAT32_SHORT_NAME_ERROR
} fat32_short_name_status_t;

fat32_short_name_status_t fat32_directory_short_name_exists(
    const fat32_volume_t *volume,
    uint32_t directory_cluster,
    const uint8_t name[FAT32_SHORT_NAME_LENGTH]);

bool fat32_directory_find_short_alias(
    const fat32_volume_t *volume,
    uint32_t directory_cluster,
    const char *filename,
    uint8_t alias[FAT32_SHORT_NAME_LENGTH]);

uint8_t fat32_lfn_checksum(const uint8_t *name);
void fat32_lfn_process(fat32_lfn_state_t *state, const fat32_long_directory_entry_t *entry);
bool fat32_lfn_validate(const fat32_lfn_state_t *state, const fat32_directory_entry_t *entry);
bool fat32_lfn_make_entry(const uint16_t *name, uint32_t name_length,
                         uint8_t order, uint8_t checksum,
                         fat32_long_directory_entry_t *entry);

bool fat32_format_long_name(const fat32_lfn_state_t *state,
                            const fat32_directory_entry_t *entry, char *name);
fat32_lookup_status_t fat32_directory_find(const fat32_volume_t *volume, uint32_t cluster,
                                          const char *name, fat32_directory_record_t *record);
fat32_lookup_status_t fat32_path_resolve(const fat32_volume_t *volume, const char *path,
                                        fat32_directory_record_t *record);
bool fat32_file_open(const fat32_volume_t *volume, const char *path, fat32_file_t *file);
void fat32_file_close(fat32_file_t *file);
int64_t fat32_file_read(fat32_file_t *file, void *buffer, size_t count);
int64_t fat32_file_seek(fat32_file_t *file, int64_t offset, fat32_seek_mode_t mode);
int64_t fat32_file_write(fat32_file_t *file, const void *buffer, size_t count);
bool fat32_file_update_metadata(fat32_file_t *file);
bool fat32_file_ensure_capacity(fat32_file_t *file, uint64_t capacity);
bool fat32_file_create(const fat32_volume_t *volume, const char *path);
bool fat32_file_delete(const fat32_volume_t *volume, const char *path);


#endif