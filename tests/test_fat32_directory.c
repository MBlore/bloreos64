
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

    RUN_TEST(test_write_single_lfn_entry);
    RUN_TEST(test_write_multiple_lfn_entries);
    RUN_TEST(test_write_lfn_across_cluster_boundary);
    RUN_TEST(test_write_lfn_preserves_end_marker);
    RUN_TEST(test_write_lfn_rejects_occupied_slot);
    RUN_TEST(test_write_lfn_failure_does_not_publish_short_entry);
    RUN_TEST(test_lfn_rollback_restores_written_entries);
    RUN_TEST(test_lfn_rollback_on_short_entry_failure);
    RUN_TEST(test_lfn_rollback_restores_end_marker);

    RUN_TEST(test_make_short_alias_basic);
    RUN_TEST(test_make_short_alias_large_sequences);
    RUN_TEST(test_make_short_alias_invalid_inputs);
    RUN_TEST(test_short_alias_collision_read_failure);
    RUN_TEST(test_find_short_alias_skips_collision);
    RUN_TEST(test_find_short_alias_read_failure);

    return UNITY_END();
}
