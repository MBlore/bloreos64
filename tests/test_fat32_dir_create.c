
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

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_directory_init_cluster);
    RUN_TEST(test_directory_init_cluster_nested_parent);

    return UNITY_END();
}
