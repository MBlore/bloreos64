
#include "unity.h"
#include "mocks/mock_fat32_directory.h"

#include <fat32.h>
#include <string.h>

#define TEST_DIRECTORY_CLUSTER (TEST_FIRST_CLUSTER + 1)
#define TEST_PARENT_CLUSTER    TEST_FIRST_CLUSTER

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
 * Verifies that a new directory contains correctly initialised
 * "." and ".." entries, followed by an end-of-directory marker.
 */
void test_directory_init_cluster(void)
{
    fat32_directory_entry_t entry;

    TEST_ASSERT_TRUE(fat32_directory_init_cluster(&volume, TEST_DIRECTORY_CLUSTER,
                                                  TEST_PARENT_CLUSTER));

    // "." must reference the new directory.
    TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_DIRECTORY_CLUSTER, 0, &entry));
    TEST_ASSERT_EQUAL_MEMORY(".          ", entry.name, FAT32_SHORT_NAME_LENGTH);
    TEST_ASSERT_EQUAL_UINT8(FAT32_ATTR_DIRECTORY, entry.attributes);
    TEST_ASSERT_EQUAL_UINT32(TEST_DIRECTORY_CLUSTER, fat32_get_first_cluster(&entry));

    // ".." must represent the root parent as cluster zero.
    TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_DIRECTORY_CLUSTER, 1, &entry));
    TEST_ASSERT_EQUAL_MEMORY("..         ", entry.name, FAT32_SHORT_NAME_LENGTH);
    TEST_ASSERT_EQUAL_UINT8(FAT32_ATTR_DIRECTORY, entry.attributes);
    TEST_ASSERT_EQUAL_UINT32(0, fat32_get_first_cluster(&entry));

    // The next entry must mark the end of the directory.
    TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_DIRECTORY_CLUSTER, 2, &entry));
    TEST_ASSERT_EQUAL_HEX8(FAT32_DIR_ENTRY_END, entry.name[0]);

    // The remaining directory entry must also be cleared.
    TEST_ASSERT_TRUE(mock_fat32_get_entry(TEST_DIRECTORY_CLUSTER, 3, &entry));
    TEST_ASSERT_EQUAL_HEX8(FAT32_DIR_ENTRY_END, entry.name[0]);
}

/*
 * Verifies that a nested directory's ".." entry references
 * its actual parent cluster rather than the FAT32 root.
 */
void test_directory_init_cluster_nested_parent(void)
{
    const uint32_t parent_cluster = TEST_FIRST_CLUSTER + 1;
    const uint32_t directory_cluster = TEST_FIRST_CLUSTER + 2;

    fat32_directory_entry_t entry;

    TEST_ASSERT_TRUE(fat32_directory_init_cluster(&volume, directory_cluster,
                                                  parent_cluster));

    // "." must reference the new directory.
    TEST_ASSERT_TRUE(mock_fat32_get_entry(directory_cluster, 0, &entry));
    TEST_ASSERT_EQUAL_UINT32(directory_cluster, fat32_get_first_cluster(&entry));
    TEST_ASSERT_EQUAL_UINT8(FAT32_ATTR_DIRECTORY, entry.attributes);

    // ".." must reference the actual parent cluster.
    TEST_ASSERT_TRUE(mock_fat32_get_entry(directory_cluster, 1, &entry));
    TEST_ASSERT_EQUAL_UINT32(parent_cluster, fat32_get_first_cluster(&entry));
    TEST_ASSERT_EQUAL_UINT8(FAT32_ATTR_DIRECTORY, entry.attributes);

    // The next entry must remain the end-of-directory marker.
    TEST_ASSERT_TRUE(mock_fat32_get_entry(directory_cluster, 2, &entry));
    TEST_ASSERT_EQUAL_HEX8(FAT32_DIR_ENTRY_END, entry.name[0]);
}

/*
 * Verifies that mkdir creates a directory entry in the root and
 * initialises the allocated cluster with "." and "..".
 */
void test_directory_create_short_name(void)
{
    fat32_directory_record_t record;
    fat32_directory_entry_t entry;

    TEST_ASSERT_TRUE(fat32_directory_create(&volume, "/DOCS"));

    // The new directory must be visible in the root.
    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_directory_find(&volume, volume.root_cluster, "DOCS", &record));

    TEST_ASSERT_EQUAL_UINT8(FAT32_ATTR_DIRECTORY, record.entry.attributes);

    uint32_t directory_cluster = fat32_get_first_cluster(&record.entry);

    TEST_ASSERT_TRUE(directory_cluster >= FAT32_FIRST_DATA_CLUSTER);
    TEST_ASSERT_TRUE(directory_cluster != volume.root_cluster);

    // "." references the new directory.
    TEST_ASSERT_TRUE(mock_fat32_get_entry(directory_cluster, 0, &entry));
    TEST_ASSERT_EQUAL_MEMORY(".          ", entry.name, FAT32_SHORT_NAME_LENGTH);
    TEST_ASSERT_EQUAL_UINT32(directory_cluster, fat32_get_first_cluster(&entry));

    // ".." references the root using FAT32's special cluster-zero convention.
    TEST_ASSERT_TRUE(mock_fat32_get_entry(directory_cluster, 1, &entry));
    TEST_ASSERT_EQUAL_MEMORY("..         ", entry.name, FAT32_SHORT_NAME_LENGTH);
    TEST_ASSERT_EQUAL_UINT32(0, fat32_get_first_cluster(&entry));

    // The directory must initially be empty.
    TEST_ASSERT_TRUE(mock_fat32_get_entry(directory_cluster, 2, &entry));
    TEST_ASSERT_EQUAL_HEX8(FAT32_DIR_ENTRY_END, entry.name[0]);
}

/*
 * Verifies that a directory can be created inside another directory
 * and that its ".." entry references the correct parent cluster.
 */
void test_directory_create_nested(void)
{
    fat32_directory_record_t parent;
    fat32_directory_record_t child;
    fat32_directory_entry_t entry;

    TEST_ASSERT_TRUE(fat32_directory_create(&volume, "/DOCS"));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_path_resolve(&volume, "/DOCS", &parent));

    uint32_t parent_cluster = fat32_get_first_cluster(&parent.entry);

    TEST_ASSERT_TRUE(fat32_directory_create(&volume, "/DOCS/PROJECTS"));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_path_resolve(&volume, "/DOCS/PROJECTS", &child));

    uint32_t child_cluster = fat32_get_first_cluster(&child.entry);

    TEST_ASSERT_NOT_EQUAL(parent_cluster, child_cluster);

    // The child's "." entry must reference itself.
    TEST_ASSERT_TRUE(mock_fat32_get_entry(child_cluster, 0, &entry));
    TEST_ASSERT_EQUAL_MEMORY(".          ", entry.name, FAT32_SHORT_NAME_LENGTH);
    TEST_ASSERT_EQUAL_UINT32(child_cluster, fat32_get_first_cluster(&entry));

    // The child's ".." entry must reference its actual parent.
    TEST_ASSERT_TRUE(mock_fat32_get_entry(child_cluster, 1, &entry));
    TEST_ASSERT_EQUAL_MEMORY("..         ", entry.name, FAT32_SHORT_NAME_LENGTH);
    TEST_ASSERT_EQUAL_UINT32(parent_cluster, fat32_get_first_cluster(&entry));
}

/*
 * Verifies that mkdir supports a long filename and that the
 * resulting directory can be resolved using its full name.
 */
void test_directory_create_long_name(void)
{
    fat32_directory_record_t record;
    fat32_directory_entry_t entry;

    TEST_ASSERT_TRUE(fat32_directory_create(&volume, "/My Documents"));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_path_resolve(&volume, "/My Documents", &record));

    TEST_ASSERT_EQUAL_STRING("My Documents", record.name);
    TEST_ASSERT_EQUAL_UINT8(FAT32_ATTR_DIRECTORY, record.entry.attributes);
    TEST_ASSERT_EQUAL_UINT8(1, record.lfn_entry_count);

    uint32_t directory_cluster = fat32_get_first_cluster(&record.entry);

    TEST_ASSERT_TRUE(directory_cluster >= FAT32_FIRST_DATA_CLUSTER);

    // Verify the new directory's "." entry.
    TEST_ASSERT_TRUE(mock_fat32_get_entry(directory_cluster, 0, &entry));
    TEST_ASSERT_EQUAL_MEMORY(".          ", entry.name, FAT32_SHORT_NAME_LENGTH);
    TEST_ASSERT_EQUAL_UINT32(directory_cluster, fat32_get_first_cluster(&entry));

    // Verify the new directory's ".." entry.
    TEST_ASSERT_TRUE(mock_fat32_get_entry(directory_cluster, 1, &entry));
    TEST_ASSERT_EQUAL_MEMORY("..         ", entry.name, FAT32_SHORT_NAME_LENGTH);
    TEST_ASSERT_EQUAL_UINT32(0, fat32_get_first_cluster(&entry));
}

/*
 * Verifies that creating a directory with an existing name fails
 * without overwriting the original directory.
 */
void test_directory_create_duplicate_name(void)
{
    fat32_directory_record_t original;
    fat32_directory_record_t current;

    TEST_ASSERT_TRUE(fat32_directory_create(&volume, "/DOCS"));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_path_resolve(&volume, "/DOCS", &original));

    uint32_t original_cluster = fat32_get_first_cluster(&original.entry);

    TEST_ASSERT_FALSE(fat32_directory_create(&volume, "/DOCS"));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_path_resolve(&volume, "/DOCS", &current));

    TEST_ASSERT_EQUAL_UINT32(original_cluster, fat32_get_first_cluster(&current.entry));
}

/*
 * Verifies that mkdir rejects invalid paths and nonexistent parents.
 */
void test_directory_create_invalid_paths(void)
{
    TEST_ASSERT_FALSE(fat32_directory_create(&volume, NULL));
    TEST_ASSERT_FALSE(fat32_directory_create(&volume, ""));
    TEST_ASSERT_FALSE(fat32_directory_create(&volume, "/"));
    TEST_ASSERT_FALSE(fat32_directory_create(&volume, "/."));
    TEST_ASSERT_FALSE(fat32_directory_create(&volume, "/.."));
    TEST_ASSERT_FALSE(fat32_directory_create(&volume, "DOCS"));

    // The parent directory must already exist.
    TEST_ASSERT_FALSE(fat32_directory_create(&volume, "/MISSING/DOCS"));
}

/*
 * Verifies that mkdir fails when no free clusters remain
 * and does not publish an entry for the failed directory.
 */
void test_directory_create_no_free_clusters(void)
{
    fat32_directory_record_t record;

    TEST_ASSERT_TRUE(fat32_directory_create(&volume, "/DOCS"));
    TEST_ASSERT_TRUE(fat32_directory_create(&volume, "/MEDIA"));

    // Both available data clusters have now been allocated.
    TEST_ASSERT_FALSE(fat32_directory_create(&volume, "/BACKUP"));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_NOT_FOUND,
                      fat32_directory_find(&volume, volume.root_cluster, "BACKUP", &record));

    // Previously created directories must remain accessible.
    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_directory_find(&volume, volume.root_cluster, "DOCS", &record));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_directory_find(&volume, volume.root_cluster, "MEDIA", &record));
}

/*
 * Verifies that mkdir releases its allocated cluster when
 * initialising the new directory fails.
 */
void test_directory_create_initialisation_write_failure(void)
{
    fat32_directory_record_t record;

    // Fail the first cluster write, which initialises "." and "..".
    mock_fat32_fail_write(1);

    TEST_ASSERT_FALSE(fat32_directory_create(&volume, "/DOCS"));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_NOT_FOUND,
                      fat32_directory_find(&volume, volume.root_cluster, "DOCS", &record));

    // Disable write failures and retry.
    mock_fat32_fail_write(0);

    // This succeeds only if the failed attempt released its cluster.
    TEST_ASSERT_TRUE(fat32_directory_create(&volume, "/DOCS"));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_directory_find(&volume, volume.root_cluster, "DOCS", &record));
}

/*
 * Verifies that mkdir releases its allocated cluster when writing
 * the new directory entry into the parent directory fails.
 */
void test_directory_create_parent_write_failure(void)
{
    fat32_directory_record_t record;
    fat32_directory_entry_t entry;

    // Write 1: initialise the new directory cluster.
    // Write 2: preserve the parent's end-of-directory marker.
    // Write 3: publish the new directory entry (simulate failure).
    mock_fat32_fail_write(3);

    TEST_ASSERT_FALSE(fat32_directory_create(&volume, "/DOCS"));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_NOT_FOUND,
                      fat32_directory_find(&volume, volume.root_cluster, "DOCS", &record));

    // The failed operation must not have published an entry.
    TEST_ASSERT_TRUE(mock_fat32_get_entry(volume.root_cluster, 0, &entry));
    TEST_ASSERT_EQUAL_HEX8(FAT32_DIR_ENTRY_END, entry.name[0]);

    // Retry after restoring successful writes.
    mock_fat32_fail_write(0);

    TEST_ASSERT_TRUE(fat32_directory_create(&volume, "/DOCS"));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_directory_find(&volume, volume.root_cluster, "DOCS", &record));

    // The released cluster should have been reused.
    TEST_ASSERT_EQUAL_UINT32(TEST_FIRST_CLUSTER + 1,
                             fat32_get_first_cluster(&record.entry));
}

/*
 * Verifies that a failed short-entry publication rolls back the
 * previously written LFN entry and releases the directory cluster.
 */
void test_directory_create_long_name_write_failure(void)
{
    fat32_directory_record_t record;
    fat32_directory_entry_t entry;

    mock_fat32_fail_write(4);

    TEST_ASSERT_FALSE(fat32_directory_create(&volume, "/My Documents"));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_NOT_FOUND,
                      fat32_directory_find(&volume, volume.root_cluster, "My Documents", &record));

    // The parent directory must retain its original end marker.
    TEST_ASSERT_TRUE(mock_fat32_get_entry(volume.root_cluster, 0, &entry));
    TEST_ASSERT_EQUAL_HEX8(FAT32_DIR_ENTRY_END, entry.name[0]);

    TEST_ASSERT_TRUE(mock_fat32_get_entry(volume.root_cluster, 1, &entry));
    TEST_ASSERT_EQUAL_HEX8(FAT32_DIR_ENTRY_END, entry.name[0]);

    TEST_ASSERT_TRUE(mock_fat32_get_entry(volume.root_cluster, 2, &entry));
    TEST_ASSERT_EQUAL_HEX8(FAT32_DIR_ENTRY_END, entry.name[0]);

    mock_fat32_fail_write(0);

    TEST_ASSERT_TRUE(fat32_directory_create(&volume, "/My Documents"));

    TEST_ASSERT_EQUAL(FAT32_LOOKUP_FOUND,
                      fat32_directory_find(&volume, volume.root_cluster, "My Documents", &record));

    TEST_ASSERT_EQUAL_UINT32(TEST_FIRST_CLUSTER + 1,
                             fat32_get_first_cluster(&record.entry));
}

/*
 * Verifies that mkdir fails cleanly when the parent directory
 * has no available entries and its cluster chain cannot grow.
 */
void test_directory_create_parent_extension_failure(void)
{
    fat32_directory_record_t record;

    // Fill every entry in the root directory.
    for (uint32_t i = 0; i < TEST_ENTRIES_PER_CLUSTER; i++) {
        mock_fat32_set_marker(volume.root_cluster, i, 'X');
    }

    // Prevent the parent directory from extending its cluster chain.
    mock_fat32_fail_extend();

    TEST_ASSERT_FALSE(fat32_directory_create(&volume, "/DOCS"));

    TEST_ASSERT_EQUAL_UINT32(0, mock_fat32_extension_count());

    // The root directory must remain unchanged.
    fat32_directory_entry_t entry;

    for (uint32_t i = 0; i < TEST_ENTRIES_PER_CLUSTER; i++) {
        TEST_ASSERT_TRUE(mock_fat32_get_entry(volume.root_cluster, i, &entry));
        TEST_ASSERT_EQUAL_UINT8('X', entry.name[0]);
    }
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_directory_init_cluster);
    RUN_TEST(test_directory_init_cluster_nested_parent);
    RUN_TEST(test_directory_create_short_name);
    RUN_TEST(test_directory_create_nested);
    RUN_TEST(test_directory_create_long_name);
    RUN_TEST(test_directory_create_duplicate_name);
    RUN_TEST(test_directory_create_invalid_paths);
    RUN_TEST(test_directory_create_no_free_clusters);
    RUN_TEST(test_directory_create_initialisation_write_failure);
    RUN_TEST(test_directory_create_parent_write_failure);
    RUN_TEST(test_directory_create_long_name_write_failure);
    RUN_TEST(test_directory_create_parent_extension_failure);
    
    return UNITY_END();
}
