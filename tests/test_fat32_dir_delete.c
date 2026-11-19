
#include "unity.h"
#include "mocks/mock_fat32_directory.h"
#include <fat32.h>
#include <string.h>

#define TEST_DIRECTORY_CLUSTER (TEST_FIRST_CLUSTER + 1)

static fat32_volume_t volume;

void setUp(void)
{
    mock_fat32_reset();

    memset(&volume, 0, sizeof(volume));
    volume.bytes_per_cluster = TEST_CLUSTER_BYTES;
    volume.cluster_count = 16;
    volume.root_cluster = TEST_FIRST_CLUSTER;
}

void tearDown(void)
{
}

/*
 * Verifies that a directory containing only "." and ".."
 * is correctly identified as empty.
 */
void test_directory_is_empty(void)
{
    TEST_ASSERT_TRUE(fat32_directory_init_cluster(&volume, TEST_DIRECTORY_CLUSTER,
                                                  volume.root_cluster));

    TEST_ASSERT_TRUE(fat32_directory_is_empty(&volume, TEST_DIRECTORY_CLUSTER));
}

/*
 * Verifies that a directory containing an active file entry
 * is not considered empty.
 */
void test_directory_is_not_empty_with_file(void)
{
    fat32_directory_entry_t entry;

    TEST_ASSERT_TRUE(fat32_directory_init_cluster(&volume, TEST_DIRECTORY_CLUSTER,
                                                  volume.root_cluster));

    // Entry 2 follows "." and "..".
    TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_DIRECTORY_CLUSTER, 2, &entry));

    memcpy(entry.name, "README  TXT", FAT32_SHORT_NAME_LENGTH);
    entry.attributes = FAT32_ATTR_ARCHIVE;

    // Mark the directory entry as occupied.
    mock_fat32_set_marker(TEST_DIRECTORY_CLUSTER, 2, 'R');

    TEST_ASSERT_FALSE(fat32_directory_is_empty(&volume, TEST_DIRECTORY_CLUSTER));
}

/*
 * Verifies that deleted directory entries do not prevent a
 * directory from being considered empty.
 */
void test_directory_is_empty_with_deleted_entries(void)
{
    TEST_ASSERT_TRUE(fat32_directory_init_cluster(&volume, TEST_DIRECTORY_CLUSTER,
                                                  volume.root_cluster));

    // Simulate a previously deleted file in entry 2.
    mock_fat32_set_marker(TEST_DIRECTORY_CLUSTER, 2, FAT32_DIR_ENTRY_DELETED);

    TEST_ASSERT_TRUE(fat32_directory_is_empty(&volume, TEST_DIRECTORY_CLUSTER));
}

/*
 * Verifies that an active long filename entry prevents a
 * directory from being considered empty.
 */
void test_directory_is_not_empty_with_lfn_entry(void)
{
    TEST_ASSERT_TRUE(fat32_directory_init_cluster(&volume, TEST_DIRECTORY_CLUSTER,
                                                  volume.root_cluster));

    // Simulate an active LFN entry following "." and "..".
    mock_fat32_set_marker(TEST_DIRECTORY_CLUSTER, 2, 0x41);

    TEST_ASSERT_FALSE(fat32_directory_is_empty(&volume, TEST_DIRECTORY_CLUSTER));
}

/*
 * Verifies that the emptiness checker follows the FAT chain
 * and detects an active entry in a subsequent cluster.
 */
void test_directory_is_not_empty_across_clusters(void)
{
    const uint32_t second_cluster = TEST_FIRST_CLUSTER + 2;

    TEST_ASSERT_TRUE(fat32_directory_init_cluster(&volume, TEST_DIRECTORY_CLUSTER,
                                                  volume.root_cluster));

    // Remove the end marker from the first cluster.
    mock_fat32_set_marker(TEST_DIRECTORY_CLUSTER, 2, FAT32_DIR_ENTRY_DELETED);
    mock_fat32_set_marker(TEST_DIRECTORY_CLUSTER, 3, FAT32_DIR_ENTRY_DELETED);

    // Link the directory's first cluster to a second cluster.
    mock_fat32_link(TEST_DIRECTORY_CLUSTER, second_cluster);
    mock_fat32_link(second_cluster, FAT32_CLUSTER_EOC);

    // Simulate a file in the second cluster.
    mock_fat32_set_marker(second_cluster, 0, 'F');

    TEST_ASSERT_FALSE(fat32_directory_is_empty(&volume, TEST_DIRECTORY_CLUSTER));
}

/*
 * Verifies that a directory spanning multiple clusters is
 * considered empty when it contains no active user entries.
 */
void test_directory_is_empty_across_clusters(void)
{
    const uint32_t second_cluster = TEST_FIRST_CLUSTER + 2;

    TEST_ASSERT_TRUE(fat32_directory_init_cluster(&volume, TEST_DIRECTORY_CLUSTER,
                                                  volume.root_cluster));

    // Fill the remaining entries in the first cluster with deleted markers.
    mock_fat32_set_marker(TEST_DIRECTORY_CLUSTER, 2, FAT32_DIR_ENTRY_DELETED);
    mock_fat32_set_marker(TEST_DIRECTORY_CLUSTER, 3, FAT32_DIR_ENTRY_DELETED);

    // Extend the directory's cluster chain.
    mock_fat32_link(TEST_DIRECTORY_CLUSTER, second_cluster);
    mock_fat32_link(second_cluster, FAT32_CLUSTER_EOC);

    // The second cluster contains only deleted entries.
    for (uint32_t i = 0; i < TEST_ENTRIES_PER_CLUSTER; i++) {
        mock_fat32_set_marker(second_cluster, i, FAT32_DIR_ENTRY_DELETED);
    }

    TEST_ASSERT_TRUE(fat32_directory_is_empty(&volume, TEST_DIRECTORY_CLUSTER));
}

/*
 * Verifies that a directory read failure is treated as an error
 * rather than incorrectly reporting the directory as empty.
 */
void test_directory_is_empty_read_failure(void)
{
    TEST_ASSERT_TRUE(fat32_directory_init_cluster(&volume, TEST_DIRECTORY_CLUSTER,
                                                  volume.root_cluster));

    // Simulate an I/O failure when reading the directory cluster.
    mock_fat32_fail_read(TEST_DIRECTORY_CLUSTER);

    TEST_ASSERT_FALSE(fat32_directory_is_empty(&volume, TEST_DIRECTORY_CLUSTER));
}

/*
 * Verifies that an invalid FAT cluster chain prevents a directory
 * from being incorrectly classified as empty.
 */
void test_directory_is_empty_invalid_chain(void)
{
    const uint32_t invalid_cluster = TEST_FIRST_CLUSTER + TEST_CLUSTER_COUNT + 10;

    TEST_ASSERT_TRUE(fat32_directory_init_cluster(&volume, TEST_DIRECTORY_CLUSTER,
                                                  volume.root_cluster));

    // Remove the end marker so the checker must follow the FAT chain.
    mock_fat32_set_marker(TEST_DIRECTORY_CLUSTER, 2, FAT32_DIR_ENTRY_DELETED);
    mock_fat32_set_marker(TEST_DIRECTORY_CLUSTER, 3, FAT32_DIR_ENTRY_DELETED);

    // Point to a cluster outside the mock's valid range.
    mock_fat32_link(TEST_DIRECTORY_CLUSTER, invalid_cluster);

    TEST_ASSERT_FALSE(fat32_directory_is_empty(&volume, TEST_DIRECTORY_CLUSTER));
}

/*
 * Verifies that an empty directory can be removed and its
 * directory entry is no longer visible to the parent.
 */
void test_directory_delete_empty(void)
{
    fat32_directory_record_t record;

    TEST_ASSERT_TRUE(fat32_directory_create(&volume, "/DOCS"));
    TEST_ASSERT_TRUE(fat32_directory_delete(&volume, "/DOCS"));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_NOT_FOUND,
                      fat32_directory_find(&volume, volume.root_cluster, "DOCS", &record));

    // The released cluster should be available for another directory.
    TEST_ASSERT_TRUE(fat32_directory_create(&volume, "/MEDIA"));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_directory_find(&volume, volume.root_cluster, "MEDIA", &record));

    TEST_ASSERT_EQUAL_UINT32(TEST_FIRST_CLUSTER + 1, fat32_get_first_cluster(&record.entry));
}

/*
 * Verifies that rmdir refuses to remove a directory containing
 * an active file entry.
 */
void test_directory_delete_non_empty(void)
{
    fat32_directory_record_t record;

    TEST_ASSERT_TRUE(fat32_directory_create(&volume, "/DOCS"));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_directory_find(&volume, volume.root_cluster, "DOCS", &record));

    uint32_t directory_cluster = fat32_get_first_cluster(&record.entry);

    // The first available entry follows "." and "..".
    mock_fat32_set_marker(directory_cluster, 2, 'F');

    TEST_ASSERT_FALSE(fat32_directory_delete(&volume, "/DOCS"));

    // The parent entry must remain intact.
    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_directory_find(&volume, volume.root_cluster, "DOCS", &record));
}

/*
 * Verifies that rmdir rejects nonexistent directories and
 * paths that do not identify a removable directory.
 */
void test_directory_delete_invalid_paths(void)
{
    TEST_ASSERT_FALSE(fat32_directory_delete(&volume, NULL));
    TEST_ASSERT_FALSE(fat32_directory_delete(&volume, ""));
    TEST_ASSERT_FALSE(fat32_directory_delete(&volume, "/"));
    TEST_ASSERT_FALSE(fat32_directory_delete(&volume, "/."));
    TEST_ASSERT_FALSE(fat32_directory_delete(&volume, "/.."));
    TEST_ASSERT_FALSE(fat32_directory_delete(&volume, "DOCS"));
    TEST_ASSERT_FALSE(fat32_directory_delete(&volume, "/MISSING"));
    TEST_ASSERT_FALSE(fat32_directory_delete(&volume, "/MISSING/DOCS"));
}

/*
 * Verifies that an empty nested directory can be removed without
 * affecting its parent directory.
 */
void test_directory_delete_nested(void)
{
    fat32_directory_record_t record;

    TEST_ASSERT_TRUE(fat32_directory_create(&volume, "/DOCS"));
    TEST_ASSERT_TRUE(fat32_directory_create(&volume, "/DOCS/PROJECTS"));

    TEST_ASSERT_TRUE(fat32_directory_delete(&volume, "/DOCS/PROJECTS"));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_NOT_FOUND,
                      fat32_path_resolve(&volume, "/DOCS/PROJECTS", &record));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_path_resolve(&volume, "/DOCS", &record));

    TEST_ASSERT_TRUE(fat32_directory_is_empty(&volume, fat32_get_first_cluster(&record.entry)));
}

/*
 * Verifies that removing a long-name directory deletes both its
 * LFN entry and its associated short-name entry.
 */
void test_directory_delete_long_name(void)
{
    fat32_directory_record_t record;
    fat32_directory_entry_t entry;

    TEST_ASSERT_TRUE(fat32_directory_create(&volume, "/My Documents"));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_path_resolve(&volume, "/My Documents", &record));

    TEST_ASSERT_EQUAL_UINT8(1, record.lfn_entry_count);

    TEST_ASSERT_TRUE(fat32_directory_delete(&volume, "/My Documents"));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_NOT_FOUND,
                      fat32_path_resolve(&volume, "/My Documents", &record));

    // Both the LFN entry and short-name entry must be deleted.
    TEST_ASSERT_TRUE(mock_fat32_get_entry(volume.root_cluster, 0, &entry));
    TEST_ASSERT_EQUAL_HEX8(FAT32_DIR_ENTRY_DELETED, entry.name[0]);

    TEST_ASSERT_TRUE(mock_fat32_get_entry(volume.root_cluster, 1, &entry));
    TEST_ASSERT_EQUAL_HEX8(FAT32_DIR_ENTRY_DELETED, entry.name[0]);
}

/*
 * Verifies that rmdir refuses to delete a regular file.
 */
void test_directory_delete_regular_file(void)
{
    fat32_directory_record_t record;
    fat32_directory_entry_t entry;

    TEST_ASSERT_TRUE(fat32_directory_create(&volume, "/DOCS"));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_path_resolve(&volume, "/DOCS", &record));

    // Convert the directory entry into a regular file entry.
    // This requires a full-entry mock write helper, so use the
    // directory writer already linked into this test suite.
    entry = record.entry;
    entry.attributes = FAT32_ATTR_ARCHIVE;

    TEST_ASSERT_TRUE(fat32_directory_write_entry(&volume, record.directory_cluster,
                                                 record.directory_index, &entry));

    TEST_ASSERT_FALSE(fat32_directory_delete(&volume, "/DOCS"));

    // The file must remain accessible.
    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_path_resolve(&volume, "/DOCS", &record));

    TEST_ASSERT_EQUAL_UINT8(FAT32_ATTR_ARCHIVE, record.entry.attributes);
}

/*
 * Verifies that failure to write the deleted short entry leaves
 * the directory visible and its allocated cluster intact.
 */
void test_directory_delete_short_entry_write_failure(void)
{
    fat32_directory_record_t record;

    TEST_ASSERT_TRUE(fat32_directory_create(&volume, "/DOCS"));

    uint32_t writes = mock_fat32_write_count();

    // The next write attempts to mark the parent entry deleted.
    mock_fat32_fail_write(writes + 1);

    TEST_ASSERT_FALSE(fat32_directory_delete(&volume, "/DOCS"));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_path_resolve(&volume, "/DOCS", &record));

    uint32_t directory_cluster = fat32_get_first_cluster(&record.entry);

    TEST_ASSERT_TRUE(fat32_directory_is_empty(&volume, directory_cluster));
}

/*
 * Verifies that an LFN directory remains accessible if deleting
 * its short-name entry fails and the LFN entry is restored.
 */
void test_directory_delete_lfn_short_entry_write_failure(void)
{
    fat32_directory_record_t record;

    TEST_ASSERT_TRUE(fat32_directory_create(&volume, "/My Documents"));

    uint32_t writes = mock_fat32_write_count();

    // First write deletes the LFN entry; second deletes the short entry.
    mock_fat32_fail_write(writes + 2);

    TEST_ASSERT_FALSE(fat32_directory_delete(&volume, "/My Documents"));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_path_resolve(&volume, "/My Documents", &record));

    TEST_ASSERT_EQUAL_UINT8(1, record.lfn_entry_count);
}

/*
 * Verifies that failure while deleting an LFN entry leaves the
 * original directory entry sequence accessible.
 */
void test_directory_delete_lfn_write_failure(void)
{
    fat32_directory_record_t record;

    TEST_ASSERT_TRUE(fat32_directory_create(&volume, "/My Documents"));

    uint32_t writes = mock_fat32_write_count();

    // Fail the first attempt to mark an LFN entry deleted.
    mock_fat32_fail_write(writes + 1);

    TEST_ASSERT_FALSE(fat32_directory_delete(&volume, "/My Documents"));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_path_resolve(&volume, "/My Documents", &record));

    TEST_ASSERT_EQUAL_UINT8(1, record.lfn_entry_count);
}

/*
 * Verifies that a FAT release failure is reported after the
 * directory has already been unlinked from its parent.
 */
void test_directory_delete_release_failure(void)
{
    fat32_directory_record_t record;

    TEST_ASSERT_TRUE(fat32_directory_create(&volume, "/DOCS"));

    mock_fat32_fail_release(true);

    TEST_ASSERT_FALSE(fat32_directory_delete(&volume, "/DOCS"));

    // The directory entry was removed before FAT release failed.
    TEST_ASSERT_EQUAL(FAT32_LOOKUP_NOT_FOUND,
                      fat32_path_resolve(&volume, "/DOCS", &record));

    mock_fat32_fail_release(false);

    // The orphaned cluster remains allocated.
    TEST_ASSERT_TRUE(fat32_directory_create(&volume, "/MEDIA"));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_path_resolve(&volume, "/MEDIA", &record));

    TEST_ASSERT_EQUAL_UINT32(TEST_FIRST_CLUSTER + 2,
                             fat32_get_first_cluster(&record.entry));
}

/*
 * Verifies that rmdir refuses to remove a directory when its
 * contents cannot be read.
 */
void test_directory_delete_read_failure(void)
{
    fat32_directory_record_t record;

    TEST_ASSERT_TRUE(fat32_directory_create(&volume, "/DOCS"));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_path_resolve(&volume, "/DOCS", &record));

    uint32_t directory_cluster = fat32_get_first_cluster(&record.entry);

    mock_fat32_fail_read(directory_cluster);

    TEST_ASSERT_FALSE(fat32_directory_delete(&volume, "/DOCS"));

    // The parent entry must remain accessible.
    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_path_resolve(&volume, "/DOCS", &record));
}

/*
 * Verifies that a directory marked read-only cannot be removed.
 */
void test_directory_delete_read_only(void)
{
    fat32_directory_record_t record;
    fat32_directory_entry_t entry;

    TEST_ASSERT_TRUE(fat32_directory_create(&volume, "/DOCS"));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_path_resolve(&volume, "/DOCS", &record));

    entry = record.entry;
    entry.attributes |= FAT32_ATTR_READ_ONLY;

    TEST_ASSERT_TRUE(fat32_directory_write_entry(&volume, record.directory_cluster,
                                                 record.directory_index, &entry));

    TEST_ASSERT_FALSE(fat32_directory_delete(&volume, "/DOCS"));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_path_resolve(&volume, "/DOCS", &record));

    TEST_ASSERT_TRUE(record.entry.attributes & FAT32_ATTR_READ_ONLY);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_directory_is_empty);
    RUN_TEST(test_directory_is_not_empty_with_file);
    RUN_TEST(test_directory_is_empty_with_deleted_entries);
    RUN_TEST(test_directory_is_not_empty_with_lfn_entry);
    RUN_TEST(test_directory_is_not_empty_across_clusters);
    RUN_TEST(test_directory_is_empty_across_clusters);
    RUN_TEST(test_directory_is_empty_read_failure);
    RUN_TEST(test_directory_is_empty_invalid_chain);
    RUN_TEST(test_directory_delete_empty);
    RUN_TEST(test_directory_delete_non_empty);
    RUN_TEST(test_directory_delete_invalid_paths);
    RUN_TEST(test_directory_delete_nested);
    RUN_TEST(test_directory_delete_long_name);
    RUN_TEST(test_directory_delete_regular_file);
    RUN_TEST(test_directory_delete_short_entry_write_failure);
    RUN_TEST(test_directory_delete_lfn_short_entry_write_failure);
    RUN_TEST(test_directory_delete_lfn_write_failure);
    RUN_TEST(test_directory_delete_release_failure);
    RUN_TEST(test_directory_delete_read_failure);
    RUN_TEST(test_directory_delete_read_only);

    return UNITY_END();
}
