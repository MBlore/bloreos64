
#include "unity.h"
#include <stdbool.h>
#include <partition.h>
#include <fat32.h>
#include <string.h>

#include "mocks/mock_fat32_directory.h"

#define TEST_OCCUPIED_MARKER 'A'

static fat32_volume_t volume;

void setUp(void)
{
    mock_fat32_reset();

    volume = (fat32_volume_t){0};
    volume.bytes_per_cluster = TEST_CLUSTER_BYTES;
    volume.cluster_count = 16;
    volume.root_cluster = TEST_FIRST_CLUSTER;
}

void tearDown(void)
{
}

static void fill_cluster(uint32_t cluster, uint8_t marker)
{
    for (uint32_t i = 0; i < TEST_ENTRIES_PER_CLUSTER; i++) {
        mock_fat32_set_marker(cluster, i, marker);
    }
}

void test_consecutive_deleted_entries(void)
{
    uint32_t cluster = 0;
    uint32_t index = 0;

    fill_cluster(TEST_FIRST_CLUSTER, TEST_OCCUPIED_MARKER);
    mock_fat32_set_marker(TEST_FIRST_CLUSTER, 1, FAT32_DIR_ENTRY_DELETED);
    mock_fat32_set_marker(TEST_FIRST_CLUSTER, 2, FAT32_DIR_ENTRY_DELETED);

    TEST_ASSERT_TRUE(fat32_directory_find_free_entries(
        &volume, TEST_FIRST_CLUSTER, 2, &cluster, &index));

    TEST_ASSERT_EQUAL_UINT32(TEST_FIRST_CLUSTER, cluster);
    TEST_ASSERT_EQUAL_UINT32(1, index);
    TEST_ASSERT_EQUAL_UINT32(0, mock_fat32_extension_count());
}

void test_occupied_entry_interrupts_run(void)
{
    uint32_t cluster = 0;
    uint32_t index = 0;

    fill_cluster(TEST_FIRST_CLUSTER, TEST_OCCUPIED_MARKER);
    mock_fat32_set_marker(TEST_FIRST_CLUSTER, 0, FAT32_DIR_ENTRY_DELETED);
    mock_fat32_set_marker(TEST_FIRST_CLUSTER, 2, FAT32_DIR_ENTRY_DELETED);
    mock_fat32_set_marker(TEST_FIRST_CLUSTER, 3, FAT32_DIR_ENTRY_DELETED);

    // Prevent extending the chain: no run of three exists.
    mock_fat32_fail_extend();

    TEST_ASSERT_FALSE(fat32_directory_find_free_entries(
        &volume, TEST_FIRST_CLUSTER, 3, &cluster, &index));
}

void test_run_spans_existing_clusters(void)
{
    uint32_t cluster = 0;
    uint32_t index = 0;

    mock_fat32_link(TEST_FIRST_CLUSTER, TEST_FIRST_CLUSTER + 1);
    mock_fat32_link(TEST_FIRST_CLUSTER + 1, FAT32_CLUSTER_EOC);

    fill_cluster(TEST_FIRST_CLUSTER, TEST_OCCUPIED_MARKER);
    fill_cluster(TEST_FIRST_CLUSTER + 1, TEST_OCCUPIED_MARKER);

    mock_fat32_set_marker(TEST_FIRST_CLUSTER, 3, FAT32_DIR_ENTRY_DELETED);
    mock_fat32_set_marker(TEST_FIRST_CLUSTER + 1, 0, FAT32_DIR_ENTRY_DELETED);
    mock_fat32_set_marker(TEST_FIRST_CLUSTER + 1, 1, FAT32_DIR_ENTRY_DELETED);

    TEST_ASSERT_TRUE(fat32_directory_find_free_entries(
        &volume, TEST_FIRST_CLUSTER, 3, &cluster, &index));

    TEST_ASSERT_EQUAL_UINT32(TEST_FIRST_CLUSTER, cluster);
    TEST_ASSERT_EQUAL_UINT32(3, index);
    TEST_ASSERT_EQUAL_UINT32(0, mock_fat32_extension_count());
}

void test_run_extends_directory_chain(void)
{
    uint32_t cluster = 0;
    uint32_t index = 0;

    fill_cluster(TEST_FIRST_CLUSTER, TEST_OCCUPIED_MARKER);
    mock_fat32_set_marker(TEST_FIRST_CLUSTER, 3, FAT32_DIR_ENTRY_DELETED);

    TEST_ASSERT_TRUE(fat32_directory_find_free_entries(
        &volume, TEST_FIRST_CLUSTER, 3, &cluster, &index));

    TEST_ASSERT_EQUAL_UINT32(TEST_FIRST_CLUSTER, cluster);
    TEST_ASSERT_EQUAL_UINT32(3, index);
    TEST_ASSERT_EQUAL_UINT32(1, mock_fat32_extension_count());
}

void test_end_marker_makes_remaining_slots_available(void)
{
    uint32_t cluster = 0;
    uint32_t index = 0;

    fill_cluster(TEST_FIRST_CLUSTER, TEST_OCCUPIED_MARKER);

    // Stale data after 0x00 must not prevent reusing the slots.
    mock_fat32_set_marker(TEST_FIRST_CLUSTER, 1, FAT32_DIR_ENTRY_END);

    TEST_ASSERT_TRUE(fat32_directory_find_free_entries(
        &volume, TEST_FIRST_CLUSTER, 3, &cluster, &index));

    TEST_ASSERT_EQUAL_UINT32(TEST_FIRST_CLUSTER, cluster);
    TEST_ASSERT_EQUAL_UINT32(1, index);
    TEST_ASSERT_EQUAL_UINT32(0, mock_fat32_extension_count());
}

void test_invalid_arguments(void)
{
    uint32_t cluster = 0;
    uint32_t index = 0;

    TEST_ASSERT_FALSE(fat32_directory_find_free_entries(
        NULL, TEST_FIRST_CLUSTER, 1, &cluster, &index));

    TEST_ASSERT_FALSE(fat32_directory_find_free_entries(
        &volume, TEST_FIRST_CLUSTER, 0, &cluster, &index));

    TEST_ASSERT_FALSE(fat32_directory_find_free_entries(
        &volume, TEST_FIRST_CLUSTER, FAT32_LFN_MAX_ENTRIES + 2,
        &cluster, &index));

    TEST_ASSERT_FALSE(fat32_directory_find_free_entries(
        &volume, TEST_FIRST_CLUSTER, 1, NULL, &index));

    TEST_ASSERT_FALSE(fat32_directory_find_free_entries(
        &volume, TEST_FIRST_CLUSTER, 1, &cluster, NULL));
}

void test_cluster_read_failure(void)
{
    uint32_t cluster = 0;
    uint32_t index = 0;

    mock_fat32_fail_read(TEST_FIRST_CLUSTER);

    TEST_ASSERT_FALSE(fat32_directory_find_free_entries(
        &volume, TEST_FIRST_CLUSTER, 2, &cluster, &index));
}

void test_cluster_extension_failure(void)
{
    uint32_t cluster = 0;
    uint32_t index = 0;

    fill_cluster(TEST_FIRST_CLUSTER, TEST_OCCUPIED_MARKER);
    mock_fat32_fail_extend();

    TEST_ASSERT_FALSE(fat32_directory_find_free_entries(
        &volume, TEST_FIRST_CLUSTER, 2, &cluster, &index));

    TEST_ASSERT_EQUAL_UINT32(0, mock_fat32_extension_count());
}


/*
 * Verifies a single LFN entry followed by its short entry.
 */
void test_write_single_lfn_entry(void)
{
    const uint16_t name[] = { 'H', 'e', 'l', 'l', 'o' };

    fat32_directory_entry_t short_entry = {0};
    memcpy(short_entry.name, "HELLO   TXT", FAT32_SHORT_NAME_LENGTH);
    short_entry.attributes = FAT32_ATTR_ARCHIVE;

    fat32_directory_entry_t first;
    fat32_directory_entry_t second;

    TEST_ASSERT_TRUE(fat32_directory_write_long_name(
        &volume, TEST_FIRST_CLUSTER, 0, name, 5, &short_entry));

    TEST_ASSERT_TRUE(mock_fat32_get_entry(
        TEST_FIRST_CLUSTER, 0, &first));
    TEST_ASSERT_TRUE(mock_fat32_get_entry(
        TEST_FIRST_CLUSTER, 1, &second));

    fat32_long_directory_entry_t lfn;
    memcpy(&lfn, &first, sizeof(lfn));

    TEST_ASSERT_EQUAL_HEX8(1 | FAT32_LFN_LAST_ENTRY, lfn.order);
    TEST_ASSERT_EQUAL_HEX8(FAT32_ATTR_LONG_NAME, lfn.attributes);
    TEST_ASSERT_EQUAL_HEX8(
        fat32_lfn_checksum(short_entry.name), lfn.checksum);

    TEST_ASSERT_EQUAL_HEX16('H', lfn.name_1[0]);
    TEST_ASSERT_EQUAL_HEX16('o', lfn.name_1[4]);
    TEST_ASSERT_EQUAL_HEX16(0, lfn.name_2[0]);
    TEST_ASSERT_EQUAL_HEX16(FAT32_LFN_PADDING, lfn.name_2[1]);

    TEST_ASSERT_EQUAL_MEMORY(&short_entry, &second, sizeof(second));
}

/*
 * Verifies that multiple LFN entries are written in reverse order.
 */
void test_write_multiple_lfn_entries(void)
{
    const uint16_t name[] = {
        'M', 'y', ' ', 'F', 'i', 'r', 's',
        't', ' ', 'D', 'o', 'c', 'u',
        'm', 'e', 'n', 't', '.', 't', 'x', 't'
    };

    fat32_directory_entry_t short_entry = {0};
    memcpy(short_entry.name, "MYFIRS~1TXT", FAT32_SHORT_NAME_LENGTH);
    short_entry.attributes = FAT32_ATTR_ARCHIVE;

    fat32_directory_entry_t first;
    fat32_directory_entry_t second;
    fat32_directory_entry_t third;

    TEST_ASSERT_TRUE(fat32_directory_write_long_name(
        &volume, TEST_FIRST_CLUSTER, 0, name, 21, &short_entry));

    TEST_ASSERT_TRUE(mock_fat32_get_entry(
        TEST_FIRST_CLUSTER, 0, &first));
    TEST_ASSERT_TRUE(mock_fat32_get_entry(
        TEST_FIRST_CLUSTER, 1, &second));
    TEST_ASSERT_TRUE(mock_fat32_get_entry(
        TEST_FIRST_CLUSTER, 2, &third));

    fat32_long_directory_entry_t lfn_first;
    fat32_long_directory_entry_t lfn_second;

    memcpy(&lfn_first, &first, sizeof(lfn_first));
    memcpy(&lfn_second, &second, sizeof(lfn_second));

    TEST_ASSERT_EQUAL_HEX8(2 | FAT32_LFN_LAST_ENTRY, lfn_first.order);
    TEST_ASSERT_EQUAL_HEX8(1, lfn_second.order);

    TEST_ASSERT_EQUAL_HEX16('m', lfn_first.name_1[0]);
    TEST_ASSERT_EQUAL_HEX16('t', lfn_first.name_2[2]);
    TEST_ASSERT_EQUAL_HEX16(0, lfn_first.name_2[3]);

    TEST_ASSERT_EQUAL_HEX16('M', lfn_second.name_1[0]);
    TEST_ASSERT_EQUAL_HEX16('u', lfn_second.name_3[1]);

    TEST_ASSERT_EQUAL_MEMORY(&short_entry, &third, sizeof(third));
}

/*
 * Verifies that writing continues into the next directory cluster.
 */
void test_write_lfn_across_cluster_boundary(void)
{
    const uint16_t name[] = {
        'L', 'o', 'n', 'g', ' ', 'F', 'i',
        'l', 'e', ' ', 'N', 'a', 'm', 'e'
    };

    fat32_directory_entry_t short_entry = {0};
    memcpy(short_entry.name, "LONGFI~1TXT", FAT32_SHORT_NAME_LENGTH);
    short_entry.attributes = FAT32_ATTR_ARCHIVE;

    mock_fat32_link(TEST_FIRST_CLUSTER, TEST_FIRST_CLUSTER + 1);
    mock_fat32_link(TEST_FIRST_CLUSTER + 1, FAT32_CLUSTER_EOC);

    fill_cluster(TEST_FIRST_CLUSTER, TEST_OCCUPIED_MARKER);

    mock_fat32_set_marker(
        TEST_FIRST_CLUSTER, 3, FAT32_DIR_ENTRY_DELETED);

    fat32_directory_entry_t first;
    fat32_directory_entry_t second;
    fat32_directory_entry_t third;

    TEST_ASSERT_TRUE(fat32_directory_write_long_name(
        &volume, TEST_FIRST_CLUSTER, 3, name, 14, &short_entry));

    TEST_ASSERT_TRUE(mock_fat32_get_entry(
        TEST_FIRST_CLUSTER, 3, &first));
    TEST_ASSERT_TRUE(mock_fat32_get_entry(
        TEST_FIRST_CLUSTER + 1, 0, &second));
    TEST_ASSERT_TRUE(mock_fat32_get_entry(
        TEST_FIRST_CLUSTER + 1, 1, &third));

    fat32_long_directory_entry_t lfn_first;
    fat32_long_directory_entry_t lfn_second;

    memcpy(&lfn_first, &first, sizeof(lfn_first));
    memcpy(&lfn_second, &second, sizeof(lfn_second));

    TEST_ASSERT_EQUAL_HEX8(2 | FAT32_LFN_LAST_ENTRY, lfn_first.order);
    TEST_ASSERT_EQUAL_HEX8(1, lfn_second.order);
    TEST_ASSERT_EQUAL_MEMORY(&short_entry, &third, sizeof(third));
}

/*
 * Verifies that a directory end marker remains after the new file.
 */
void test_write_lfn_preserves_end_marker(void)
{
    const uint16_t name[] = { 'T', 'e', 's', 't' };

    fat32_directory_entry_t short_entry = {0};
    memcpy(short_entry.name, "TEST    TXT", FAT32_SHORT_NAME_LENGTH);
    short_entry.attributes = FAT32_ATTR_ARCHIVE;

    fill_cluster(TEST_FIRST_CLUSTER, TEST_OCCUPIED_MARKER);

    mock_fat32_set_marker(
        TEST_FIRST_CLUSTER, 1, FAT32_DIR_ENTRY_END);

    TEST_ASSERT_TRUE(fat32_directory_write_long_name(
        &volume, TEST_FIRST_CLUSTER, 1, name, 4, &short_entry));

    fat32_directory_entry_t end_entry;

    TEST_ASSERT_TRUE(mock_fat32_get_entry(
        TEST_FIRST_CLUSTER, 3, &end_entry));

    TEST_ASSERT_EQUAL_HEX8(FAT32_DIR_ENTRY_END, end_entry.name[0]);
}

/*
 * Verifies that an occupied slot prevents all writes.
 */
void test_write_lfn_rejects_occupied_slot(void)
{
    const uint16_t name[] = { 'T', 'e', 's', 't' };

    fat32_directory_entry_t short_entry = {0};
    memcpy(short_entry.name, "TEST    TXT", FAT32_SHORT_NAME_LENGTH);

    fill_cluster(TEST_FIRST_CLUSTER, TEST_OCCUPIED_MARKER);

    mock_fat32_set_marker(
        TEST_FIRST_CLUSTER, 0, FAT32_DIR_ENTRY_DELETED);

    TEST_ASSERT_FALSE(fat32_directory_write_long_name(
        &volume, TEST_FIRST_CLUSTER, 0, name, 4, &short_entry));

    TEST_ASSERT_EQUAL_UINT32(0, mock_fat32_write_count());
}

/*
 * Verifies that an LFN write failure does not publish the short entry.
 */
void test_write_lfn_failure_does_not_publish_short_entry(void)
{
    const uint16_t name[] = { 'T', 'e', 's', 't' };

    fat32_directory_entry_t short_entry = {0};
    memcpy(short_entry.name, "TEST    TXT", FAT32_SHORT_NAME_LENGTH);

    // The first write preserves the end marker.
    // The second write attempts to write the LFN entry.
    mock_fat32_fail_write(2);

    TEST_ASSERT_FALSE(fat32_directory_write_long_name(
        &volume, TEST_FIRST_CLUSTER, 0, name, 4, &short_entry));

    fat32_directory_entry_t entry;

    TEST_ASSERT_TRUE(mock_fat32_get_entry(
        TEST_FIRST_CLUSTER, 1, &entry));

    TEST_ASSERT_EQUAL_HEX8(FAT32_DIR_ENTRY_END, entry.name[0]);
    TEST_ASSERT_EQUAL_UINT32(4, mock_fat32_write_count());
}

/*
 * Verifies that failure on the second LFN write restores
 * all modified directory entries to their original contents.
 */
void test_lfn_rollback_restores_written_entries(void)
{
    const uint16_t name[] = {
        'L', 'o', 'n', 'g', ' ', 'F', 'i',
        'l', 'e', ' ', 'N', 'a', 'm', 'e'
    };

    fat32_directory_entry_t short_entry = {0};
    memcpy(short_entry.name, "LONGFI~1TXT", FAT32_SHORT_NAME_LENGTH);
    short_entry.attributes = FAT32_ATTR_ARCHIVE;

    fill_cluster(TEST_FIRST_CLUSTER, TEST_OCCUPIED_MARKER);
    mock_fat32_set_marker(TEST_FIRST_CLUSTER, 0, FAT32_DIR_ENTRY_END);

    // Save the original contents of every directory slot.
    fat32_directory_entry_t original[TEST_ENTRIES_PER_CLUSTER];

    for (uint32_t i = 0; i < TEST_ENTRIES_PER_CLUSTER; i++) {
        TEST_ASSERT_TRUE(mock_fat32_get_entry(
            TEST_FIRST_CLUSTER, i, &original[i]));
    }

    // Write 1: move the end marker.
    // Write 2: first LFN entry succeeds.
    // Write 3: second LFN entry fails.
    mock_fat32_fail_write(3);

    TEST_ASSERT_FALSE(fat32_directory_write_long_name(
        &volume, TEST_FIRST_CLUSTER, 0, name, 14, &short_entry));

    // Every slot should match its original contents after rollback.
    for (uint32_t i = 0; i < TEST_ENTRIES_PER_CLUSTER; i++) {
        fat32_directory_entry_t actual;

        TEST_ASSERT_TRUE(mock_fat32_get_entry(
            TEST_FIRST_CLUSTER, i, &actual));

        TEST_ASSERT_EQUAL_MEMORY(&original[i], &actual, sizeof(actual));
    }
}

/*
 * Verifies that a failed short-entry write restores the entire
 * LFN sequence without publishing the new file.
 */
void test_lfn_rollback_on_short_entry_failure(void)
{
    const uint16_t name[] = {
        'L', 'o', 'n', 'g', ' ', 'F', 'i',
        'l', 'e', ' ', 'N', 'a', 'm', 'e'
    };

    fat32_directory_entry_t short_entry = {0};
    memcpy(short_entry.name, "LONGFI~1TXT", FAT32_SHORT_NAME_LENGTH);
    short_entry.attributes = FAT32_ATTR_ARCHIVE;

    // Use deleted entries so no end-marker relocation is needed.
    fill_cluster(TEST_FIRST_CLUSTER, FAT32_DIR_ENTRY_DELETED);

    fat32_directory_entry_t original[3];

    for (uint32_t i = 0; i < 3; i++) {
        TEST_ASSERT_TRUE(mock_fat32_get_entry(
            TEST_FIRST_CLUSTER, i, &original[i]));
    }

    // Write 1: first LFN entry.
    // Write 2: second LFN entry.
    // Write 3: short-name entry fails.
    mock_fat32_fail_write(3);

    TEST_ASSERT_FALSE(fat32_directory_write_long_name(
        &volume, TEST_FIRST_CLUSTER, 0, name, 14, &short_entry));

    // Verify all three entries were restored.
    for (uint32_t i = 0; i < 3; i++) {
        fat32_directory_entry_t actual;

        TEST_ASSERT_TRUE(mock_fat32_get_entry(
            TEST_FIRST_CLUSTER, i, &actual));

        TEST_ASSERT_EQUAL_MEMORY(&original[i], &actual, sizeof(actual));
    }

    // Three writes attempted, followed by three rollback writes.
    TEST_ASSERT_EQUAL_UINT32(6, mock_fat32_write_count());
}

/*
 * Verifies that rollback restores the original directory end marker
 * and preserves stale data following it.
 */
void test_lfn_rollback_restores_end_marker(void)
{
    const uint16_t name[] = { 'T', 'e', 's', 't' };

    fat32_directory_entry_t short_entry = {0};
    memcpy(short_entry.name, "TEST    TXT", FAT32_SHORT_NAME_LENGTH);
    short_entry.attributes = FAT32_ATTR_ARCHIVE;

    // Start with occupied entries and an end marker at index 1.
    fill_cluster(TEST_FIRST_CLUSTER, TEST_OCCUPIED_MARKER);
    mock_fat32_set_marker(TEST_FIRST_CLUSTER, 1, FAT32_DIR_ENTRY_END);

    fat32_directory_entry_t original_end;
    fat32_directory_entry_t original_stale;

    TEST_ASSERT_TRUE(mock_fat32_get_entry(
        TEST_FIRST_CLUSTER, 1, &original_end));

    TEST_ASSERT_TRUE(mock_fat32_get_entry(
        TEST_FIRST_CLUSTER, 3, &original_stale));

    // Write 1: move the end marker to index 3.
    // Write 2: attempt the LFN entry (fails).
    // Rollback must restore both original entries.
    mock_fat32_fail_write(2);

    TEST_ASSERT_FALSE(fat32_directory_write_long_name(
        &volume, TEST_FIRST_CLUSTER, 1, name, 4, &short_entry));

    fat32_directory_entry_t actual_end;
    fat32_directory_entry_t actual_stale;

    TEST_ASSERT_TRUE(mock_fat32_get_entry(
        TEST_FIRST_CLUSTER, 1, &actual_end));

    TEST_ASSERT_TRUE(mock_fat32_get_entry(
        TEST_FIRST_CLUSTER, 3, &actual_stale));

    TEST_ASSERT_EQUAL_MEMORY(
        &original_end, &actual_end, sizeof(actual_end));

    TEST_ASSERT_EQUAL_MEMORY(
        &original_stale, &actual_stale, sizeof(actual_stale));

    // Two write attempts followed by two rollback writes.
    TEST_ASSERT_EQUAL_UINT32(4, mock_fat32_write_count());
}

/*
 * Verifies that a long filename produces the expected numbered
 * FAT32 short-name alias.
 */
void test_make_short_alias_basic(void)
{
    uint8_t alias[FAT32_SHORT_NAME_LENGTH];

    TEST_ASSERT_TRUE(fat32_make_short_alias(
        "My First Document.txt", 1, alias));

    TEST_ASSERT_EQUAL_MEMORY(
        "MYFIRS~1TXT", alias, FAT32_SHORT_NAME_LENGTH);

    // A different sequence number must produce a different alias.
    TEST_ASSERT_TRUE(fat32_make_short_alias(
        "My First Document.txt", 2, alias));

    TEST_ASSERT_EQUAL_MEMORY(
        "MYFIRS~2TXT", alias, FAT32_SHORT_NAME_LENGTH);
}

/*
 * Verifies that the alias formatter adjusts the base length
 * as the numeric suffix grows.
 */
void test_make_short_alias_large_sequences(void)
{
    uint8_t alias[FAT32_SHORT_NAME_LENGTH];

    TEST_ASSERT_TRUE(fat32_make_short_alias(
        "My First Document.txt", 9, alias));

    TEST_ASSERT_EQUAL_MEMORY(
        "MYFIRS~9TXT", alias, FAT32_SHORT_NAME_LENGTH);

    TEST_ASSERT_TRUE(fat32_make_short_alias(
        "My First Document.txt", 10, alias));

    TEST_ASSERT_EQUAL_MEMORY(
        "MYFIR~10TXT", alias, FAT32_SHORT_NAME_LENGTH);

    TEST_ASSERT_TRUE(fat32_make_short_alias(
        "My First Document.txt", 999999, alias));

    TEST_ASSERT_EQUAL_MEMORY(
        "M~999999TXT", alias, FAT32_SHORT_NAME_LENGTH);
}

/*
 * Verifies that invalid filenames and sequence numbers are rejected
 * without attempting to generate a short-name alias.
 */
void test_make_short_alias_invalid_inputs(void)
{
    uint8_t alias[FAT32_SHORT_NAME_LENGTH];

    TEST_ASSERT_FALSE(fat32_make_short_alias(
        NULL, 1, alias));

    TEST_ASSERT_FALSE(fat32_make_short_alias(
        "", 1, alias));

    TEST_ASSERT_FALSE(fat32_make_short_alias(
        ".txt", 1, alias));

    TEST_ASSERT_FALSE(fat32_make_short_alias(
        "My Document.txt", 0, alias));

    TEST_ASSERT_FALSE(fat32_make_short_alias(
        "My Document.txt", 1000000, alias));

    TEST_ASSERT_FALSE(fat32_make_short_alias(
        "Folder/File.txt", 1, alias));

    TEST_ASSERT_FALSE(fat32_make_short_alias(
        "Folder\\File.txt", 1, alias));

    TEST_ASSERT_FALSE(fat32_make_short_alias(
        "My Document.txt", 1, NULL));

    // A filename without an extension is valid.
    TEST_ASSERT_TRUE(fat32_make_short_alias(
        "My Document", 1, alias));

    TEST_ASSERT_EQUAL_MEMORY(
        "MYDOCU~1   ", alias, FAT32_SHORT_NAME_LENGTH);
}

/*
 * Verifies that an existing short-name alias is detected
 * and a different alias is reported as available.
 */
void test_short_alias_collision_detection(void)
{
    uint8_t existing_alias[FAT32_SHORT_NAME_LENGTH];
    uint8_t different_alias[FAT32_SHORT_NAME_LENGTH];

    TEST_ASSERT_TRUE(fat32_make_short_alias(
        "My First Document.txt", 1, existing_alias));

    TEST_ASSERT_TRUE(fat32_make_short_alias(
        "My First Document.txt", 2, different_alias));

    fill_cluster(TEST_FIRST_CLUSTER, FAT32_DIR_ENTRY_DELETED);

    fat32_directory_entry_t entry = {0};
    memcpy(entry.name, existing_alias, FAT32_SHORT_NAME_LENGTH);
    entry.attributes = FAT32_ATTR_ARCHIVE;

    TEST_ASSERT_TRUE(fat32_directory_write_entry(
        &volume, TEST_FIRST_CLUSTER, 0, &entry));

    TEST_ASSERT_EQUAL(
        FAT32_SHORT_NAME_FOUND,
        fat32_directory_short_name_exists(
            &volume, TEST_FIRST_CLUSTER, existing_alias));

    TEST_ASSERT_EQUAL(
        FAT32_SHORT_NAME_NOT_FOUND,
        fat32_directory_short_name_exists(
            &volume, TEST_FIRST_CLUSTER, different_alias));
}

/*
 * Verifies that a directory read failure is reported as an error,
 * rather than incorrectly indicating that an alias is available.
 */
void test_short_alias_collision_read_failure(void)
{
    uint8_t alias[FAT32_SHORT_NAME_LENGTH];

    TEST_ASSERT_TRUE(fat32_make_short_alias(
        "My First Document.txt", 1, alias));

    fill_cluster(TEST_FIRST_CLUSTER, FAT32_DIR_ENTRY_DELETED);

    // Simulate a disk read failure on the directory cluster.
    mock_fat32_fail_read(TEST_FIRST_CLUSTER);

    TEST_ASSERT_EQUAL(
        FAT32_SHORT_NAME_ERROR,
        fat32_directory_short_name_exists(
            &volume, TEST_FIRST_CLUSTER, alias));
}

/*
 * Verifies that alias selection skips an occupied short name
 * and returns the next available numbered alias.
 */
void test_find_short_alias_skips_collision(void)
{
    uint8_t existing_alias[FAT32_SHORT_NAME_LENGTH];
    uint8_t selected_alias[FAT32_SHORT_NAME_LENGTH];

    TEST_ASSERT_TRUE(fat32_make_short_alias(
        "My First Document.txt", 1, existing_alias));

    // Begin with deleted directory entries.
    fill_cluster(TEST_FIRST_CLUSTER, FAT32_DIR_ENTRY_DELETED);

    // Occupy the first candidate: MYFIRS~1.TXT.
    fat32_directory_entry_t entry = {0};
    memcpy(entry.name, existing_alias, FAT32_SHORT_NAME_LENGTH);
    entry.attributes = FAT32_ATTR_ARCHIVE;

    TEST_ASSERT_TRUE(fat32_directory_write_entry(
        &volume, TEST_FIRST_CLUSTER, 0, &entry));

    // The selector must skip ~1 and choose ~2.
    TEST_ASSERT_TRUE(fat32_directory_find_short_alias(
        &volume, TEST_FIRST_CLUSTER,
        "My First Document.txt", selected_alias));

    TEST_ASSERT_EQUAL_MEMORY(
        "MYFIRS~2TXT", selected_alias, FAT32_SHORT_NAME_LENGTH);
}

/*
 * Verifies that alias selection aborts when the directory cannot
 * be read, rather than treating the error as an available alias.
 */
void test_find_short_alias_read_failure(void)
{
    uint8_t selected_alias[FAT32_SHORT_NAME_LENGTH];

    fill_cluster(TEST_FIRST_CLUSTER, FAT32_DIR_ENTRY_DELETED);

    // Simulate a disk read failure on the directory cluster.
    mock_fat32_fail_read(TEST_FIRST_CLUSTER);

    TEST_ASSERT_FALSE(fat32_directory_find_short_alias(
        &volume, TEST_FIRST_CLUSTER,
        "My First Document.txt", selected_alias));
}

/*
 * Verifies that the allocator begins at the directory end marker,
 * rather than selecting stale entries beyond it.
 */
void test_find_free_entries_starts_at_end_marker(void)
{
    // Slot 0 is occupied.
    // Slot 1 is the logical end of the directory.
    // Slots 2 and 3 contain stale data.
    fill_cluster(TEST_FIRST_CLUSTER, TEST_OCCUPIED_MARKER);
    mock_fat32_set_marker(TEST_FIRST_CLUSTER, 1, FAT32_DIR_ENTRY_END);

    uint32_t entry_cluster = 0;
    uint32_t entry_index = 0;

    // An LFN entry and its short-name entry require two slots.
    TEST_ASSERT_TRUE(fat32_directory_find_free_entries(
        &volume, TEST_FIRST_CLUSTER, 2, &entry_cluster, &entry_index));

    TEST_ASSERT_EQUAL_UINT32(TEST_FIRST_CLUSTER, entry_cluster);
    TEST_ASSERT_EQUAL_UINT32(1, entry_index);
}

/*
 * Verifies that an LFN sequence can replace the directory end
 * marker and that a new end marker follows the created file.
 */
void test_write_lfn_replaces_end_marker(void)
{
    const uint16_t name[] = { 'T', 'e', 's', 't' };

    fat32_directory_entry_t short_entry = {0};
    memcpy(short_entry.name, "TEST~1  TXT", FAT32_SHORT_NAME_LENGTH);
    short_entry.attributes = FAT32_ATTR_ARCHIVE;

    // Slot 0 is occupied, slot 1 is the end marker,
    // and slots 2 and 3 contain stale directory data.
    fill_cluster(TEST_FIRST_CLUSTER, TEST_OCCUPIED_MARKER);
    mock_fat32_set_marker(TEST_FIRST_CLUSTER, 1, FAT32_DIR_ENTRY_END);

    uint32_t entry_cluster;
    uint32_t entry_index;

    TEST_ASSERT_TRUE(fat32_directory_find_free_entries(
        &volume, TEST_FIRST_CLUSTER, 2, &entry_cluster, &entry_index));

    TEST_ASSERT_EQUAL_UINT32(TEST_FIRST_CLUSTER, entry_cluster);
    TEST_ASSERT_EQUAL_UINT32(1, entry_index);

    TEST_ASSERT_TRUE(fat32_directory_write_long_name(
        &volume, entry_cluster, entry_index, name, 4, &short_entry));

    fat32_directory_entry_t lfn_entry;
    fat32_directory_entry_t actual_short;
    fat32_directory_entry_t end_entry;

    TEST_ASSERT_TRUE(mock_fat32_get_entry(
        TEST_FIRST_CLUSTER, 1, &lfn_entry));

    TEST_ASSERT_TRUE(mock_fat32_get_entry(
        TEST_FIRST_CLUSTER, 2, &actual_short));

    TEST_ASSERT_TRUE(mock_fat32_get_entry(
        TEST_FIRST_CLUSTER, 3, &end_entry));

    TEST_ASSERT_EQUAL_HEX8(FAT32_ATTR_LONG_NAME, lfn_entry.attributes);

    TEST_ASSERT_EQUAL_MEMORY(
        short_entry.name, actual_short.name, FAT32_SHORT_NAME_LENGTH);

    TEST_ASSERT_EQUAL_HEX8(FAT32_DIR_ENTRY_END, end_entry.name[0]);
}

/*
 * Verifies that an LFN sequence can use deleted entries
 * immediately before the end-of-directory marker.
 *
 * The existing end marker must remain in place when the
 * allocated sequence does not consume it.
 */
void test_write_lfn_before_end_marker(void)
{
    const uint16_t name[] = { 'T', 'e', 's', 't' };

    fat32_directory_entry_t short_entry = {0};
    memcpy(short_entry.name, "TEST~1  TXT", FAT32_SHORT_NAME_LENGTH);
    short_entry.attributes = FAT32_ATTR_ARCHIVE;

    // Slots 0 and 1 are deleted; slot 2 ends the directory.
    // Slot 3 contains stale data.
    fill_cluster(TEST_FIRST_CLUSTER, TEST_OCCUPIED_MARKER);
    mock_fat32_set_marker(TEST_FIRST_CLUSTER, 0, FAT32_DIR_ENTRY_DELETED);
    mock_fat32_set_marker(TEST_FIRST_CLUSTER, 1, FAT32_DIR_ENTRY_DELETED);
    mock_fat32_set_marker(TEST_FIRST_CLUSTER, 2, FAT32_DIR_ENTRY_END);

    uint32_t entry_cluster;
    uint32_t entry_index;

    TEST_ASSERT_TRUE(fat32_directory_find_free_entries(
        &volume, TEST_FIRST_CLUSTER, 2, &entry_cluster, &entry_index));

    TEST_ASSERT_EQUAL_UINT32(TEST_FIRST_CLUSTER, entry_cluster);
    TEST_ASSERT_EQUAL_UINT32(0, entry_index);

    TEST_ASSERT_TRUE(fat32_directory_write_long_name(
        &volume, entry_cluster, entry_index, name, 4, &short_entry));

    fat32_directory_entry_t lfn_entry;
    fat32_directory_entry_t actual_short;
    fat32_directory_entry_t end_entry;

    TEST_ASSERT_TRUE(mock_fat32_get_entry(
        TEST_FIRST_CLUSTER, 0, &lfn_entry));

    TEST_ASSERT_TRUE(mock_fat32_get_entry(
        TEST_FIRST_CLUSTER, 1, &actual_short));

    TEST_ASSERT_TRUE(mock_fat32_get_entry(
        TEST_FIRST_CLUSTER, 2, &end_entry));

    TEST_ASSERT_EQUAL_HEX8(FAT32_ATTR_LONG_NAME, lfn_entry.attributes);

    TEST_ASSERT_EQUAL_MEMORY(
        short_entry.name, actual_short.name, FAT32_SHORT_NAME_LENGTH);

    TEST_ASSERT_EQUAL_HEX8(FAT32_DIR_ENTRY_END, end_entry.name[0]);
}

/*
 * Verifies that an ASCII filename is converted into the UTF-16
 * code units required by FAT32 long filename entries.
 */
void test_filename_to_utf16(void)
{
    const char *filename = "My First Document.txt";
    const uint16_t expected[] = {
        'M', 'y', ' ', 'F', 'i', 'r', 's', 't', ' ', 'D', 'o',
        'c', 'u', 'm', 'e', 'n', 't', '.', 't', 'x', 't'
    };

    uint16_t output[FAT32_LFN_MAX_LENGTH] = {0};
    uint32_t length = 0;

    TEST_ASSERT_TRUE(fat32_filename_to_utf16(filename, output, &length));

    TEST_ASSERT_EQUAL_UINT32(sizeof(expected) / sizeof(expected[0]), length);
    TEST_ASSERT_EQUAL_UINT16_ARRAY(expected, output, length);
}

/*
 * Verifies that ASCII-to-UTF-16 conversion rejects unsupported characters
 * and filenames exceeding FAT32's maximum long filename length.
 */
void test_filename_to_utf16_invalid_inputs(void)
{
    uint16_t output[FAT32_LFN_MAX_LENGTH] = {0};
    uint32_t length = 0;

    TEST_ASSERT_FALSE(fat32_filename_to_utf16(NULL, output, &length));
    TEST_ASSERT_FALSE(fat32_filename_to_utf16("", output, &length));
    TEST_ASSERT_FALSE(fat32_filename_to_utf16("Test/File.txt", output, &length));
    TEST_ASSERT_FALSE(fat32_filename_to_utf16("Test\\File.txt", output, &length));
    TEST_ASSERT_FALSE(fat32_filename_to_utf16("Test\nFile.txt", output, &length));
    TEST_ASSERT_FALSE(fat32_filename_to_utf16("Caf\xC3\xA9.txt", output, &length));
    TEST_ASSERT_FALSE(fat32_filename_to_utf16("Test.txt", NULL, &length));
    TEST_ASSERT_FALSE(fat32_filename_to_utf16("Test.txt", output, NULL));

    // Exactly 255 characters is permitted by this conversion function.
    char maximum_name[FAT32_LFN_MAX_LENGTH + 1];

    for (uint32_t i = 0; i < FAT32_LFN_MAX_LENGTH; i++) {
        maximum_name[i] = 'A';
    }

    maximum_name[FAT32_LFN_MAX_LENGTH] = '\0';

    TEST_ASSERT_TRUE(fat32_filename_to_utf16(maximum_name, output, &length));
    TEST_ASSERT_EQUAL_UINT32(FAT32_LFN_MAX_LENGTH, length);

    // One additional character must be rejected.
    char oversized_name[FAT32_LFN_MAX_LENGTH + 2];

    for (uint32_t i = 0; i < FAT32_LFN_MAX_LENGTH + 1; i++) {
        oversized_name[i] = 'A';
    }

    oversized_name[FAT32_LFN_MAX_LENGTH + 1] = '\0';

    TEST_ASSERT_FALSE(fat32_filename_to_utf16(oversized_name, output, &length));
}

/*
 * Verifies that FAT32-invalid filename characters and trailing
 * spaces or dots are rejected during UTF-16 conversion.
 */
void test_filename_to_utf16_rejects_invalid_names(void)
{
    uint16_t output[FAT32_LFN_MAX_LENGTH];
    uint32_t length = 0;

    const char *invalid_names[] = {
        "File:Name.txt",
        "File*Name.txt",
        "File?Name.txt",
        "File\"Name.txt",
        "File<Name.txt",
        "File>Name.txt",
        "File|Name.txt",
        "File.txt.",
        "File.txt ",
        ".",
        ".."
    };

    for (uint32_t i = 0; i < sizeof(invalid_names) / sizeof(invalid_names[0]); i++) {
        TEST_ASSERT_FALSE(fat32_filename_to_utf16(invalid_names[i], output, &length));
    }

    // Spaces and dots within a filename remain valid.
    TEST_ASSERT_TRUE(fat32_filename_to_utf16("My Document.v2.txt", output, &length));
    TEST_ASSERT_EQUAL_UINT32(18, length);
}

/*
 * Verifies that creating a long-named file writes its LFN entries
 * followed by a short directory entry containing a unique alias.
 */
void test_file_create_long_filename(void)
{
    const char *filename = "My First Document.txt";

    fat32_directory_entry_t first;
    fat32_directory_entry_t second;
    fat32_directory_entry_t short_entry;

    // An empty directory starts with the end-of-directory marker.
    TEST_ASSERT_TRUE(fat32_file_create(&volume, "/My First Document.txt"));

    // The 21-character filename requires two LFN entries.
    TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_FIRST_CLUSTER, 0, &first));
    TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_FIRST_CLUSTER, 1, &second));
    TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_FIRST_CLUSTER, 2, &short_entry));

    TEST_ASSERT_EQUAL_HEX8(FAT32_ATTR_LONG_NAME, first.attributes);
    TEST_ASSERT_EQUAL_HEX8(FAT32_ATTR_LONG_NAME, second.attributes);

    // The first on-disk LFN entry carries the LAST_ENTRY flag.
    TEST_ASSERT_EQUAL_HEX8(0x42, first.name[0]);
    TEST_ASSERT_EQUAL_HEX8(0x01, second.name[0]);

    TEST_ASSERT_EQUAL_MEMORY("MYFIRS~1TXT", short_entry.name, FAT32_SHORT_NAME_LENGTH);
    TEST_ASSERT_EQUAL_HEX8(FAT32_ATTR_ARCHIVE, short_entry.attributes);
    TEST_ASSERT_EQUAL_UINT32(0, short_entry.file_size);

    // Both LFN entries must reference the same short filename.
    const fat32_long_directory_entry_t *lfn_first =
        (const fat32_long_directory_entry_t *)&first;

    const fat32_long_directory_entry_t *lfn_second =
        (const fat32_long_directory_entry_t *)&second;

    uint8_t checksum = fat32_lfn_checksum(short_entry.name);

    TEST_ASSERT_EQUAL_HEX8(checksum, lfn_first->checksum);
    TEST_ASSERT_EQUAL_HEX8(checksum, lfn_second->checksum);

    // Confirm that the original filename can be found by directory lookup.
    fat32_directory_record_t record;

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_directory_find(&volume, volume.root_cluster, filename, &record));

    TEST_ASSERT_EQUAL_MEMORY(short_entry.name, record.entry.name, FAT32_SHORT_NAME_LENGTH);
}

/*
 * Verifies that creating a long-named file skips an occupied short
 * alias and selects the next available numbered alias.
 */
void test_file_create_long_filename_alias_collision(void)
{
    const char *filename = "My First Document.txt";

    uint8_t existing_alias[FAT32_SHORT_NAME_LENGTH];

    TEST_ASSERT_TRUE(fat32_make_short_alias(filename, 1, existing_alias));

    // Occupy the first candidate alias with an existing regular file.
    fat32_directory_entry_t existing = {0};

    memcpy(existing.name, existing_alias, FAT32_SHORT_NAME_LENGTH);
    existing.attributes = FAT32_ATTR_ARCHIVE;

    TEST_ASSERT_TRUE(fat32_directory_write_entry(&volume, TEST_FIRST_CLUSTER, 0, &existing));

    // Place the directory end marker immediately after the existing file.
    mock_fat32_set_marker(TEST_FIRST_CLUSTER, 1, FAT32_DIR_ENTRY_END);

    TEST_ASSERT_TRUE(fat32_file_create(&volume, "/My First Document.txt"));

    fat32_directory_entry_t first_lfn;
    fat32_directory_entry_t second_lfn;
    fat32_directory_entry_t short_entry;

    TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_FIRST_CLUSTER, 1, &first_lfn));
    TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_FIRST_CLUSTER, 2, &second_lfn));
    TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_FIRST_CLUSTER, 3, &short_entry));

    TEST_ASSERT_EQUAL_HEX8(FAT32_ATTR_LONG_NAME, first_lfn.attributes);
    TEST_ASSERT_EQUAL_HEX8(FAT32_ATTR_LONG_NAME, second_lfn.attributes);
    TEST_ASSERT_EQUAL_MEMORY("MYFIRS~2TXT", short_entry.name, FAT32_SHORT_NAME_LENGTH);

    // The original entry must remain untouched.
    fat32_directory_entry_t preserved;

    TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_FIRST_CLUSTER, 0, &preserved));
    TEST_ASSERT_EQUAL_MEMORY(existing_alias, preserved.name, FAT32_SHORT_NAME_LENGTH);
}

/*
 * Verifies that attempting to create an existing long filename fails
 * without changing its directory entries or allocating additional space.
 */
void test_file_create_duplicate_long_filename(void)
{
    const char *path = "/My First Document.txt";

    TEST_ASSERT_TRUE(fat32_file_create(&volume, path));

    fat32_directory_entry_t original[3];

    for (uint32_t i = 0; i < 3; i++) {
        TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_FIRST_CLUSTER, i, &original[i]));
    }

    uint32_t writes_before = mock_fat32_write_count();
    uint32_t extensions_before = mock_fat32_extension_count();

    // Creating the same file again must fail.
    TEST_ASSERT_FALSE(fat32_file_create(&volume, path));

    TEST_ASSERT_EQUAL_UINT32(writes_before, mock_fat32_write_count());
    TEST_ASSERT_EQUAL_UINT32(extensions_before, mock_fat32_extension_count());

    // The existing LFN sequence and short entry must remain unchanged.
    for (uint32_t i = 0; i < 3; i++) {
        fat32_directory_entry_t current;

        TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_FIRST_CLUSTER, i, &current));
        TEST_ASSERT_EQUAL_MEMORY(&original[i], &current, sizeof(current));
    }
}

/*
 * Verifies that a failed LFN write does not publish the short entry
 * or leave a partially created file visible in the directory.
 */
void test_file_create_long_filename_write_failure(void)
{
    const char *filename = "My First Document.txt";

    // Fail the second directory write during creation.
    mock_fat32_fail_write(2);

    TEST_ASSERT_FALSE(fat32_file_create(&volume, "/My First Document.txt"));

    fat32_directory_record_t record;

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_NOT_FOUND,
                      fat32_directory_find(&volume, volume.root_cluster, filename, &record));

    // The short entry must not have been published.
    fat32_directory_entry_t entry;

    TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_FIRST_CLUSTER, 2, &entry));
    TEST_ASSERT_EQUAL_HEX8(FAT32_DIR_ENTRY_END, entry.name[0]);
}

/*
 * Verifies that deleting a long filename removes its associated LFN
 * entries and short directory entry without affecting adjacent slots.
 */
void test_file_delete_long_filename(void)
{
    fat32_directory_record_t record;
    fat32_directory_entry_t entry;

    TEST_ASSERT_TRUE(fat32_file_create(&volume, "/My First Document.txt"));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_directory_find(&volume, volume.root_cluster,
                                           "My First Document.txt", &record));

    TEST_ASSERT_EQUAL_UINT8(2, record.lfn_entry_count);
    TEST_ASSERT_EQUAL_UINT32(TEST_FIRST_CLUSTER, record.directory_cluster);
    TEST_ASSERT_EQUAL_UINT32(2, record.directory_index);

    TEST_ASSERT_TRUE(fat32_file_delete(&volume, "/My First Document.txt"));

    // Both LFN entries must be marked deleted.
    TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_FIRST_CLUSTER, 0, &entry));
    TEST_ASSERT_EQUAL_HEX8(FAT32_DIR_ENTRY_DELETED, entry.name[0]);

    TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_FIRST_CLUSTER, 1, &entry));
    TEST_ASSERT_EQUAL_HEX8(FAT32_DIR_ENTRY_DELETED, entry.name[0]);

    // The associated short entry must also be deleted.
    TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_FIRST_CLUSTER, 2, &entry));
    TEST_ASSERT_EQUAL_HEX8(FAT32_DIR_ENTRY_DELETED, entry.name[0]);

    // The end-of-directory marker must remain intact.
    TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_FIRST_CLUSTER, 3, &entry));
    TEST_ASSERT_EQUAL_HEX8(FAT32_DIR_ENTRY_END, entry.name[0]);

    // The deleted filename must no longer be found.
    TEST_ASSERT_EQUAL(FAT32_LOOKUP_NOT_FOUND,
        fat32_directory_find(&volume, volume.root_cluster, "My First Document.txt", &record));
}

/*
 * Verifies that directory lookup reports the number of validated LFN
 * entries and distinguishes long filenames from ordinary 8.3 names.
 */
void test_directory_record_lfn_entry_count(void)
{
    fat32_directory_record_t record;

    // A 21-character filename requires two LFN entries.
    TEST_ASSERT_TRUE(fat32_file_create(&volume, "/My First Document.txt"));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_directory_find(&volume, volume.root_cluster, "My First Document.txt",
                                           &record));

    TEST_ASSERT_EQUAL_UINT8(2, record.lfn_entry_count);
    TEST_ASSERT_EQUAL_UINT32(TEST_FIRST_CLUSTER, record.directory_cluster);
    TEST_ASSERT_EQUAL_UINT32(2, record.directory_index);

    // An ordinary 8.3 filename must not report any LFN entries.
    TEST_ASSERT_TRUE(fat32_file_create(&volume, "/TEST.TXT"));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_directory_find(&volume, volume.root_cluster, "TEST.TXT", &record));

    TEST_ASSERT_EQUAL_UINT8(0, record.lfn_entry_count);
}

/*
 * Verifies that the start of an LFN sequence can be located
 * when all its entries reside in the same directory cluster.
 */
void test_find_lfn_start_same_cluster(void)
{
    fat32_directory_record_t record;
    uint32_t start_cluster = 0;
    uint32_t start_index = 0;

    TEST_ASSERT_TRUE(fat32_file_create(&volume, "/My First Document.txt"));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_directory_find(&volume, volume.root_cluster, "My First Document.txt",
                                           &record));

    TEST_ASSERT_EQUAL_UINT8(2, record.lfn_entry_count);

    TEST_ASSERT_TRUE(fat32_directory_find_lfn_start(&volume, volume.root_cluster, &record,
                                                    &start_cluster, &start_index));

    TEST_ASSERT_EQUAL_UINT32(TEST_FIRST_CLUSTER, start_cluster);
    TEST_ASSERT_EQUAL_UINT32(0, start_index);
}

/*
 * Verifies that the beginning of an LFN sequence is located correctly
 * when the sequence spans two directory clusters.
 */
void test_find_lfn_start_across_cluster_boundary(void)
{
    fat32_directory_record_t record;
    uint32_t start_cluster = 0;
    uint32_t start_index = 0;

    // Link two directory clusters together.
    mock_fat32_link(TEST_FIRST_CLUSTER, TEST_FIRST_CLUSTER + 1);
    mock_fat32_link(TEST_FIRST_CLUSTER + 1, FAT32_CLUSTER_EOC);

    // Occupy the first three entries, leaving the final slot available.
    fill_cluster(TEST_FIRST_CLUSTER, TEST_OCCUPIED_MARKER);
    mock_fat32_set_marker(TEST_FIRST_CLUSTER, 3, FAT32_DIR_ENTRY_DELETED);

    // The next cluster begins with an end-of-directory marker.
    mock_fat32_set_marker(TEST_FIRST_CLUSTER + 1, 0, FAT32_DIR_ENTRY_END);

    TEST_ASSERT_TRUE(fat32_file_create(&volume, "/My First Document.txt"));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_directory_find(&volume, volume.root_cluster, "My First Document.txt",
                                           &record));

    TEST_ASSERT_EQUAL_UINT8(2, record.lfn_entry_count);
    TEST_ASSERT_EQUAL_UINT32(TEST_FIRST_CLUSTER + 1, record.directory_cluster);
    TEST_ASSERT_EQUAL_UINT32(1, record.directory_index);

    TEST_ASSERT_TRUE(fat32_directory_find_lfn_start(&volume, volume.root_cluster, &record,
                                                    &start_cluster, &start_index));

    TEST_ASSERT_EQUAL_UINT32(TEST_FIRST_CLUSTER, start_cluster);
    TEST_ASSERT_EQUAL_UINT32(3, start_index);
}

/*
 * Verifies that LFN validation accepts an intact sequence and rejects
 * a sequence whose checksum no longer matches its short entry.
 */
void test_directory_validate_lfn_checksum(void)
{
    fat32_directory_record_t record;
    fat32_directory_entry_t entry;

    TEST_ASSERT_TRUE(fat32_file_create(&volume, "/My First Document.txt"));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_directory_find(&volume, volume.root_cluster, "My First Document.txt",
                                           &record));

    TEST_ASSERT_EQUAL_UINT8(2, record.lfn_entry_count);

    // An unchanged LFN sequence must validate successfully.
    TEST_ASSERT_TRUE(fat32_directory_validate_lfn(&volume, volume.root_cluster, &record));

    // Corrupt the checksum in the first LFN entry.
    TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_FIRST_CLUSTER, 0, &entry));

    fat32_long_directory_entry_t *lfn = (fat32_long_directory_entry_t *)&entry;
    lfn->checksum ^= 0xFF;

    TEST_ASSERT_TRUE(fat32_directory_write_entry(&volume, TEST_FIRST_CLUSTER, 0, &entry));

    // The original record must no longer validate.
    TEST_ASSERT_FALSE(fat32_directory_validate_lfn(&volume, volume.root_cluster, &record));
}

/*
 * Verifies that an LFN sequence spanning two directory clusters can
 * be validated, and that corruption in the second cluster is detected.
 */
void test_directory_validate_lfn_across_cluster_boundary(void)
{
    fat32_directory_record_t record;
    fat32_directory_entry_t entry;

    mock_fat32_link(TEST_FIRST_CLUSTER, TEST_FIRST_CLUSTER + 1);
    mock_fat32_link(TEST_FIRST_CLUSTER + 1, FAT32_CLUSTER_EOC);

    // Leave one free entry at the end of the first cluster.
    fill_cluster(TEST_FIRST_CLUSTER, TEST_OCCUPIED_MARKER);
    mock_fat32_set_marker(TEST_FIRST_CLUSTER, 3, FAT32_DIR_ENTRY_DELETED);

    // The next cluster starts with an end-of-directory marker.
    mock_fat32_set_marker(TEST_FIRST_CLUSTER + 1, 0, FAT32_DIR_ENTRY_END);

    TEST_ASSERT_TRUE(fat32_file_create(&volume, "/My First Document.txt"));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_directory_find(&volume, volume.root_cluster, "My First Document.txt",
                                           &record));

    TEST_ASSERT_EQUAL_UINT8(2, record.lfn_entry_count);
    TEST_ASSERT_TRUE(fat32_directory_validate_lfn(&volume, volume.root_cluster, &record));

    // Corrupt the LFN entry located in the second cluster.
    TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_FIRST_CLUSTER + 1, 0, &entry));

    fat32_long_directory_entry_t *lfn = (fat32_long_directory_entry_t *)&entry;
    lfn->checksum ^= 0xFF;

    TEST_ASSERT_TRUE(fat32_directory_write_entry(&volume, TEST_FIRST_CLUSTER + 1, 0, &entry));

    TEST_ASSERT_FALSE(fat32_directory_validate_lfn(&volume, volume.root_cluster, &record));
}

/*
 * Verifies that changing the LFN characters without changing their
 * checksum invalidates a previously resolved directory record.
 */
void test_directory_validate_lfn_name_corruption(void)
{
    fat32_directory_record_t record;
    fat32_directory_entry_t entry;

    TEST_ASSERT_TRUE(fat32_file_create(&volume, "/My First Document.txt"));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_directory_find(&volume, volume.root_cluster, "My First Document.txt",
                                           &record));

    TEST_ASSERT_EQUAL_UINT8(2, record.lfn_entry_count);
    TEST_ASSERT_TRUE(fat32_directory_validate_lfn(&volume, volume.root_cluster, &record));

    // Change the first UTF-16 character without modifying the checksum.
    TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_FIRST_CLUSTER, 1, &entry));

    fat32_long_directory_entry_t *lfn = (fat32_long_directory_entry_t *)&entry;
    lfn->name_1[0] = 'X';

    TEST_ASSERT_TRUE(fat32_directory_write_entry(&volume, TEST_FIRST_CLUSTER, 1, &entry));

    // The original directory record must no longer validate.
    TEST_ASSERT_FALSE(fat32_directory_validate_lfn(&volume, volume.root_cluster, &record));
}

/*
 * Verifies that deleting an LFN sequence marks only its long filename
 * entries as deleted, preserving the associated short directory entry.
 */
void test_directory_delete_lfn_same_cluster(void)
{
    fat32_directory_record_t record;
    fat32_directory_entry_t entry;

    TEST_ASSERT_TRUE(fat32_file_create(&volume, "/My First Document.txt"));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_directory_find(&volume, volume.root_cluster,
                                           "My First Document.txt", &record));

    TEST_ASSERT_EQUAL_UINT8(2, record.lfn_entry_count);
    TEST_ASSERT_TRUE(fat32_directory_validate_lfn(&volume, volume.root_cluster, &record));

    TEST_ASSERT_TRUE(fat32_directory_delete_lfn(&volume, volume.root_cluster, &record));

    // Both LFN entries should now be marked as deleted.
    TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_FIRST_CLUSTER, 0, &entry));
    TEST_ASSERT_EQUAL_HEX8(FAT32_DIR_ENTRY_DELETED, entry.name[0]);

    TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_FIRST_CLUSTER, 1, &entry));
    TEST_ASSERT_EQUAL_HEX8(FAT32_DIR_ENTRY_DELETED, entry.name[0]);

    // The short entry must remain unchanged.
    TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_FIRST_CLUSTER, 2, &entry));
    TEST_ASSERT_EQUAL_MEMORY(&record.entry, &entry, sizeof(entry));
}

/*
 * Verifies that deleting an LFN sequence spanning two clusters marks
 * both LFN entries as deleted without modifying the short entry.
 */
void test_directory_delete_lfn_across_cluster_boundary(void)
{
    fat32_directory_record_t record;
    fat32_directory_entry_t entry;

    mock_fat32_link(TEST_FIRST_CLUSTER, TEST_FIRST_CLUSTER + 1);
    mock_fat32_link(TEST_FIRST_CLUSTER + 1, FAT32_CLUSTER_EOC);

    // Leave one available slot at the end of the first cluster.
    fill_cluster(TEST_FIRST_CLUSTER, TEST_OCCUPIED_MARKER);
    mock_fat32_set_marker(TEST_FIRST_CLUSTER, 3, FAT32_DIR_ENTRY_DELETED);

    // The next cluster starts with an end-of-directory marker.
    mock_fat32_set_marker(TEST_FIRST_CLUSTER + 1, 0, FAT32_DIR_ENTRY_END);

    TEST_ASSERT_TRUE(fat32_file_create(&volume, "/My First Document.txt"));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_directory_find(&volume, volume.root_cluster,
                                           "My First Document.txt", &record));

    TEST_ASSERT_EQUAL_UINT8(2, record.lfn_entry_count);
    TEST_ASSERT_EQUAL_UINT32(TEST_FIRST_CLUSTER + 1, record.directory_cluster);
    TEST_ASSERT_EQUAL_UINT32(1, record.directory_index);

    TEST_ASSERT_TRUE(fat32_directory_validate_lfn(&volume, volume.root_cluster, &record));
    TEST_ASSERT_TRUE(fat32_directory_delete_lfn(&volume, volume.root_cluster, &record));

    // First LFN entry is at the end of cluster 2.
    TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_FIRST_CLUSTER, 3, &entry));
    TEST_ASSERT_EQUAL_HEX8(FAT32_DIR_ENTRY_DELETED, entry.name[0]);

    // Second LFN entry is at the beginning of cluster 3.
    TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_FIRST_CLUSTER + 1, 0, &entry));
    TEST_ASSERT_EQUAL_HEX8(FAT32_DIR_ENTRY_DELETED, entry.name[0]);

    // The short entry must remain unchanged.
    TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_FIRST_CLUSTER + 1, 1, &entry));
    TEST_ASSERT_EQUAL_MEMORY(&record.entry, &entry, sizeof(entry));
}

/*
 * Verifies that deleting a long filename spanning two directory
 * clusters removes all associated entries without modifying
 * unrelated directory entries.
 */
void test_file_delete_long_filename_across_cluster_boundary(void)
{
    fat32_directory_record_t record;
    fat32_directory_entry_t entry;

    mock_fat32_link(TEST_FIRST_CLUSTER, TEST_FIRST_CLUSTER + 1);
    mock_fat32_link(TEST_FIRST_CLUSTER + 1, FAT32_CLUSTER_EOC);

    // Occupy the first three entries in the root directory.
    fill_cluster(TEST_FIRST_CLUSTER, TEST_OCCUPIED_MARKER);
    mock_fat32_set_marker(TEST_FIRST_CLUSTER, 3, FAT32_DIR_ENTRY_DELETED);

    // The second cluster begins with the end-of-directory marker.
    mock_fat32_set_marker(TEST_FIRST_CLUSTER + 1, 0, FAT32_DIR_ENTRY_END);

    TEST_ASSERT_TRUE(fat32_file_create(&volume, "/My First Document.txt"));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_directory_find(&volume, volume.root_cluster,
                                           "My First Document.txt", &record));

    TEST_ASSERT_EQUAL_UINT8(2, record.lfn_entry_count);
    TEST_ASSERT_EQUAL_UINT32(TEST_FIRST_CLUSTER + 1, record.directory_cluster);
    TEST_ASSERT_EQUAL_UINT32(1, record.directory_index);

    TEST_ASSERT_TRUE(fat32_file_delete(&volume, "/My First Document.txt"));

    // The first LFN entry is in cluster 2, index 3.
    TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_FIRST_CLUSTER, 3, &entry));
    TEST_ASSERT_EQUAL_HEX8(FAT32_DIR_ENTRY_DELETED, entry.name[0]);

    // The second LFN entry is in cluster 3, index 0.
    TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_FIRST_CLUSTER + 1, 0, &entry));
    TEST_ASSERT_EQUAL_HEX8(FAT32_DIR_ENTRY_DELETED, entry.name[0]);

    // The short entry is in cluster 3, index 1.
    TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_FIRST_CLUSTER + 1, 1, &entry));
    TEST_ASSERT_EQUAL_HEX8(FAT32_DIR_ENTRY_DELETED, entry.name[0]);

    // The end-of-directory marker remains intact.
    TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_FIRST_CLUSTER + 1, 2, &entry));
    TEST_ASSERT_EQUAL_HEX8(FAT32_DIR_ENTRY_END, entry.name[0]);

    // The original filename must no longer be found.
    TEST_ASSERT_EQUAL(FAT32_LOOKUP_NOT_FOUND,
                      fat32_directory_find(&volume, volume.root_cluster,
                                           "My First Document.txt", &record));
}

/*
 * Verifies that a failure writing the first LFN entry prevents deletion
 * of the short entry and preserves the original directory contents.
 */
void test_file_delete_long_filename_first_write_failure(void)
{
    fat32_directory_record_t record;
    fat32_directory_entry_t before[3];
    fat32_directory_entry_t after;

    TEST_ASSERT_TRUE(fat32_file_create(&volume, "/My First Document.txt"));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_directory_find(&volume, volume.root_cluster,
                                           "My First Document.txt", &record));

    // Preserve all three entries before attempting deletion.
    for (uint32_t index = 0; index < 3; index++) {
        TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_FIRST_CLUSTER, index, &before[index]));
    }

    // Fail the first write performed by the deletion operation.
    mock_fat32_fail_write(mock_fat32_write_count() + 1);

    TEST_ASSERT_FALSE(fat32_file_delete(&volume, "/My First Document.txt"));

    // No directory entry should have changed.
    for (uint32_t index = 0; index < 3; index++) {
        TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_FIRST_CLUSTER, index, &after));
        TEST_ASSERT_EQUAL_MEMORY(&before[index], &after, sizeof(after));
    }

    // The original filename must still resolve successfully.
    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_directory_find(&volume, volume.root_cluster,
                                           "My First Document.txt", &record));
}

/*
 * Verifies that a failure deleting the second LFN entry triggers
 * rollback of the first entry, preserving the complete filename.
 */
void test_file_delete_long_filename_second_write_failure(void)
{
    fat32_directory_record_t record;
    fat32_directory_entry_t before[3];
    fat32_directory_entry_t after;

    TEST_ASSERT_TRUE(fat32_file_create(&volume, "/My First Document.txt"));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_directory_find(&volume, volume.root_cluster,
                                           "My First Document.txt", &record));

    TEST_ASSERT_EQUAL_UINT8(2, record.lfn_entry_count);

    // Preserve the original LFN and short directory entries.
    for (uint32_t index = 0; index < 3; index++) {
        TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_FIRST_CLUSTER, index, &before[index]));
    }

    // Fail the second deletion write, allowing rollback writes to succeed.
    mock_fat32_fail_write(mock_fat32_write_count() + 2);

    TEST_ASSERT_FALSE(fat32_file_delete(&volume, "/My First Document.txt"));

    // All three directory entries must match their original contents.
    for (uint32_t index = 0; index < 3; index++) {
        TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_FIRST_CLUSTER, index, &after));
        TEST_ASSERT_EQUAL_MEMORY(&before[index], &after, sizeof(after));
    }

    // The original long filename must still resolve after rollback.
    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_directory_find(&volume, volume.root_cluster,
                                           "My First Document.txt", &record));
}

/*
 * Verifies that a failed LFN deletion write restores an earlier entry
 * even when the original entries occupy different directory clusters.
 */
void test_file_delete_long_filename_cross_cluster_rollback(void)
{
    fat32_directory_record_t record;
    fat32_directory_entry_t before[3];
    fat32_directory_entry_t after;

    mock_fat32_link(TEST_FIRST_CLUSTER, TEST_FIRST_CLUSTER + 1);
    mock_fat32_link(TEST_FIRST_CLUSTER + 1, FAT32_CLUSTER_EOC);

    // Leave one free slot at the end of the first cluster.
    fill_cluster(TEST_FIRST_CLUSTER, TEST_OCCUPIED_MARKER);
    mock_fat32_set_marker(TEST_FIRST_CLUSTER, 3, FAT32_DIR_ENTRY_DELETED);
    mock_fat32_set_marker(TEST_FIRST_CLUSTER + 1, 0, FAT32_DIR_ENTRY_END);

    TEST_ASSERT_TRUE(fat32_file_create(&volume, "/My First Document.txt"));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_directory_find(&volume, volume.root_cluster,
                                           "My First Document.txt", &record));

    TEST_ASSERT_EQUAL_UINT8(2, record.lfn_entry_count);

    // Preserve the two LFN entries and their associated short entry.
    TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_FIRST_CLUSTER, 3, &before[0]));
    TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_FIRST_CLUSTER + 1, 0, &before[1]));
    TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_FIRST_CLUSTER + 1, 1, &before[2]));

    // Fail the second deletion write after the first LFN entry is modified.
    mock_fat32_fail_write(mock_fat32_write_count() + 2);

    TEST_ASSERT_FALSE(fat32_file_delete(&volume, "/My First Document.txt"));

    // The first LFN entry must be restored in the original cluster.
    TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_FIRST_CLUSTER, 3, &after));
    TEST_ASSERT_EQUAL_MEMORY(&before[0], &after, sizeof(after));

    // The second LFN entry must remain unchanged.
    TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_FIRST_CLUSTER + 1, 0, &after));
    TEST_ASSERT_EQUAL_MEMORY(&before[1], &after, sizeof(after));

    // The short entry must also remain unchanged.
    TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_FIRST_CLUSTER + 1, 1, &after));
    TEST_ASSERT_EQUAL_MEMORY(&before[2], &after, sizeof(after));

    // The original long filename must remain accessible.
    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_directory_find(&volume, volume.root_cluster,
                                           "My First Document.txt", &record));
}

void test_file_delete_long_filename_short_entry_write_failure(void)
{
    fat32_directory_record_t record;
    fat32_directory_entry_t before[3];
    fat32_directory_entry_t after;

    TEST_ASSERT_TRUE(fat32_file_create(&volume, "/My First Document.txt"));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_directory_find(&volume, volume.root_cluster,
                                           "My First Document.txt", &record));

    TEST_ASSERT_EQUAL_UINT8(2, record.lfn_entry_count);

    // Preserve the original two LFN entries and short entry.
    for (uint32_t index = 0; index < 3; index++) {
        TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_FIRST_CLUSTER, index, &before[index]));
    }

    // Allow both LFN deletion writes, then fail the short-entry write.
    mock_fat32_fail_write(mock_fat32_write_count() + 3);

    TEST_ASSERT_FALSE(fat32_file_delete(&volume, "/My First Document.txt"));

    // All entries should be restored to their original contents.
    for (uint32_t index = 0; index < 3; index++) {
        TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_FIRST_CLUSTER, index, &after));
        TEST_ASSERT_EQUAL_MEMORY(&before[index], &after, sizeof(after));
    }

    // The original long filename should remain accessible.
    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_directory_find(&volume, volume.root_cluster,
                                           "My First Document.txt", &record));
}

/*
 * Verifies that resolving the parent of a root-level file returns
 * the root directory cluster and the correct filename.
 */
void test_path_resolve_parent_root_file(void)
{
    uint32_t parent_cluster = 0;
    char filename[FAT32_UTF8_NAME_BUFFER_SIZE];

    TEST_ASSERT_TRUE(fat32_path_resolve_parent(&volume, "/readme.txt",
                                               &parent_cluster, filename));

    TEST_ASSERT_EQUAL_UINT32(volume.root_cluster, parent_cluster);
    TEST_ASSERT_EQUAL_STRING("readme.txt", filename);
}

/*
 * Verifies that resolving a nested file path returns the parent
 * directory's starting cluster and the final filename component.
 */
void test_path_resolve_parent_nested_file(void)
{
    uint32_t parent_cluster = 0;
    char filename[FAT32_UTF8_NAME_BUFFER_SIZE];

    // Create a valid FAT32 short directory entry.
    fat32_directory_entry_t directory;

    memset(&directory, 0, sizeof(directory));
    memcpy(directory.name, "DOCS       ", FAT32_SHORT_NAME_LENGTH);

    directory.attributes = FAT32_ATTR_DIRECTORY;
    directory.first_cluster_low = TEST_FIRST_CLUSTER + 1;

    TEST_ASSERT_TRUE(fat32_directory_write_entry(&volume, TEST_FIRST_CLUSTER, 0, &directory));

    TEST_ASSERT_TRUE(fat32_path_resolve_parent(&volume, "/docs/readme.txt",
                                               &parent_cluster, filename));

    TEST_ASSERT_EQUAL_UINT32(TEST_FIRST_CLUSTER + 1, parent_cluster);
    TEST_ASSERT_EQUAL_STRING("readme.txt", filename);
}

/*
 * Verifies that parent resolution fails when an intermediate
 * directory does not exist.
 */
void test_path_resolve_parent_missing_directory(void)
{
    uint32_t parent_cluster = 0;
    char filename[FAT32_UTF8_NAME_BUFFER_SIZE];

    TEST_ASSERT_FALSE(fat32_path_resolve_parent(&volume, "/missing/readme.txt",
                                                &parent_cluster, filename));
}

/*
 * Verifies that parent resolution rejects a regular file
 * when a directory is required.
 */
void test_path_resolve_parent_regular_file(void)
{
    uint32_t parent_cluster = 0;
    char filename[FAT32_UTF8_NAME_BUFFER_SIZE];

    // Create a regular file in the root directory.
    TEST_ASSERT_TRUE(fat32_file_create(&volume, "/docs"));

    // A regular file cannot contain another file.
    TEST_ASSERT_FALSE(fat32_path_resolve_parent(&volume, "/docs/readme.txt",
                                                &parent_cluster, filename));
}

/*
 * Verifies that creating a file using a nested path writes the new
 * directory entry into the parent directory rather than the root.
 */
void test_file_create_nested_directory(void)
{
    fat32_directory_entry_t directory;
    fat32_directory_record_t record;

    // Create a DOCS directory entry in the root.
    memset(&directory, 0, sizeof(directory));
    memcpy(directory.name, "DOCS       ", FAT32_SHORT_NAME_LENGTH);

    directory.attributes = FAT32_ATTR_DIRECTORY;
    directory.first_cluster_low = TEST_FIRST_CLUSTER + 1;

    TEST_ASSERT_TRUE(fat32_directory_write_entry(&volume, TEST_FIRST_CLUSTER, 0, &directory));

    // Create a file inside DOCS.
    TEST_ASSERT_TRUE(fat32_file_create(&volume, "/docs/readme.txt"));

    // The file must exist inside the DOCS directory.
    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_directory_find(&volume, TEST_FIRST_CLUSTER + 1,
                                           "readme.txt", &record));

    TEST_ASSERT_EQUAL_UINT32(TEST_FIRST_CLUSTER + 1, record.directory_cluster);
    TEST_ASSERT_EQUAL_STRING("README.TXT", record.name);

    // The file must not appear in the root directory.
    TEST_ASSERT_EQUAL(FAT32_LOOKUP_NOT_FOUND,
                      fat32_directory_find(&volume, volume.root_cluster,
                                           "readme.txt", &record));
}

/*
 * Verifies that a long filename can be created inside an existing
 * directory and that its original casing is preserved.
 */
void test_file_create_nested_long_filename(void)
{
    fat32_directory_entry_t directory;
    fat32_directory_record_t record;

    // Create the DOCS directory entry in the root.
    memset(&directory, 0, sizeof(directory));
    memcpy(directory.name, "DOCS       ", FAT32_SHORT_NAME_LENGTH);

    directory.attributes = FAT32_ATTR_DIRECTORY;
    directory.first_cluster_low = TEST_FIRST_CLUSTER + 1;

    TEST_ASSERT_TRUE(fat32_directory_write_entry(&volume, TEST_FIRST_CLUSTER, 0, &directory));

    // Create a long filename inside DOCS.
    TEST_ASSERT_TRUE(fat32_file_create(&volume, "/docs/My First Document.txt"));

    // Verify that the file exists inside DOCS.
    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_directory_find(&volume, TEST_FIRST_CLUSTER + 1,
                                           "My First Document.txt", &record));

    TEST_ASSERT_EQUAL_STRING("My First Document.txt", record.name);
    TEST_ASSERT_EQUAL_UINT32(TEST_FIRST_CLUSTER + 1, record.directory_cluster);
    TEST_ASSERT_EQUAL_UINT8(2, record.lfn_entry_count);

    // Verify that the file does not exist in the root directory.
    TEST_ASSERT_EQUAL(FAT32_LOOKUP_NOT_FOUND,
                      fat32_directory_find(&volume, volume.root_cluster,
                                           "My First Document.txt", &record));
}

/*
 * Verifies that deleting a long filename from a nested directory
 * removes its LFN and short entries without modifying the parent.
 */
void test_file_delete_nested_long_filename(void)
{
    fat32_directory_entry_t directory;
    fat32_directory_entry_t entry;
    fat32_directory_record_t record;

    // Create a DOCS directory entry in the root.
    memset(&directory, 0, sizeof(directory));
    memcpy(directory.name, "DOCS       ", FAT32_SHORT_NAME_LENGTH);

    directory.attributes = FAT32_ATTR_DIRECTORY;
    directory.first_cluster_low = TEST_FIRST_CLUSTER + 1;

    TEST_ASSERT_TRUE(fat32_directory_write_entry(&volume, TEST_FIRST_CLUSTER, 0, &directory));

    // Create and verify a long filename inside DOCS.
    TEST_ASSERT_TRUE(fat32_file_create(&volume, "/docs/My First Document.txt"));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_directory_find(&volume, TEST_FIRST_CLUSTER + 1,
                                           "My First Document.txt", &record));

    TEST_ASSERT_EQUAL_UINT8(2, record.lfn_entry_count);

    // Delete the nested file.
    TEST_ASSERT_TRUE(fat32_file_delete(&volume, "/docs/My First Document.txt"));

    // Both LFN entries and the short entry must be marked deleted.
    for (uint32_t index = 0; index < 3; index++) {
        TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_FIRST_CLUSTER + 1, index, &entry));
        TEST_ASSERT_EQUAL_HEX8(FAT32_DIR_ENTRY_DELETED, entry.name[0]);
    }

    // The deleted filename must no longer resolve.
    TEST_ASSERT_EQUAL(FAT32_LOOKUP_NOT_FOUND,
                      fat32_directory_find(&volume, TEST_FIRST_CLUSTER + 1,
                                           "My First Document.txt", &record));

    // The parent directory must still exist.
    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_directory_find(&volume, volume.root_cluster, "docs", &record));

    TEST_ASSERT_TRUE(record.entry.attributes & FAT32_ATTR_DIRECTORY);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_consecutive_deleted_entries);
    RUN_TEST(test_occupied_entry_interrupts_run);
    RUN_TEST(test_run_spans_existing_clusters);
    RUN_TEST(test_run_extends_directory_chain);
    RUN_TEST(test_end_marker_makes_remaining_slots_available);
    RUN_TEST(test_invalid_arguments);
    RUN_TEST(test_cluster_read_failure);
    RUN_TEST(test_cluster_extension_failure);
    RUN_TEST(test_find_free_entries_starts_at_end_marker);
    
    RUN_TEST(test_write_single_lfn_entry);
    RUN_TEST(test_write_multiple_lfn_entries);
    RUN_TEST(test_write_lfn_across_cluster_boundary);
    RUN_TEST(test_write_lfn_preserves_end_marker);
    RUN_TEST(test_write_lfn_rejects_occupied_slot);
    RUN_TEST(test_write_lfn_failure_does_not_publish_short_entry);
    RUN_TEST(test_lfn_rollback_restores_written_entries);
    RUN_TEST(test_lfn_rollback_on_short_entry_failure);
    RUN_TEST(test_lfn_rollback_restores_end_marker);
    RUN_TEST(test_write_lfn_replaces_end_marker);
    RUN_TEST(test_write_lfn_before_end_marker);

    RUN_TEST(test_make_short_alias_basic);
    RUN_TEST(test_make_short_alias_large_sequences);
    RUN_TEST(test_make_short_alias_invalid_inputs);
    RUN_TEST(test_short_alias_collision_read_failure);
    RUN_TEST(test_find_short_alias_skips_collision);
    RUN_TEST(test_find_short_alias_read_failure);

    RUN_TEST(test_filename_to_utf16);
    RUN_TEST(test_filename_to_utf16_invalid_inputs);
    RUN_TEST(test_filename_to_utf16_rejects_invalid_names);

    RUN_TEST(test_file_create_long_filename);
    RUN_TEST(test_file_create_long_filename_alias_collision);
    RUN_TEST(test_file_create_duplicate_long_filename);
    RUN_TEST(test_file_create_long_filename_write_failure);
    RUN_TEST(test_file_delete_long_filename);
    RUN_TEST(test_file_delete_long_filename_across_cluster_boundary);
    RUN_TEST(test_file_delete_long_filename_first_write_failure);
    RUN_TEST(test_file_delete_long_filename_second_write_failure);
    RUN_TEST(test_file_delete_long_filename_cross_cluster_rollback);
    RUN_TEST(test_file_delete_long_filename_short_entry_write_failure);
    RUN_TEST(test_file_delete_nested_long_filename);
    RUN_TEST(test_file_create_nested_directory);
    RUN_TEST(test_file_create_nested_long_filename);

    RUN_TEST(test_find_lfn_start_same_cluster);
    RUN_TEST(test_find_lfn_start_across_cluster_boundary);

    RUN_TEST(test_directory_record_lfn_entry_count);
    RUN_TEST(test_directory_validate_lfn_checksum);
    RUN_TEST(test_directory_validate_lfn_across_cluster_boundary);
    RUN_TEST(test_directory_validate_lfn_name_corruption);
    RUN_TEST(test_directory_delete_lfn_same_cluster);
    RUN_TEST(test_directory_delete_lfn_across_cluster_boundary);

    RUN_TEST(test_path_resolve_parent_root_file);
    RUN_TEST(test_path_resolve_parent_nested_file);
    RUN_TEST(test_path_resolve_parent_missing_directory);
    RUN_TEST(test_path_resolve_parent_regular_file);
    
    return UNITY_END();
}
