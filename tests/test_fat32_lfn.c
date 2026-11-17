#include "unity.h"
#include <stdbool.h>
#include <partition.h>
#include <fat32.h>

void setUp(void)
{
}

void tearDown(void)
{
}

void test_lfn_single_entry(void)
{
    const uint16_t name[] = {'H', 'e', 'l', 'l', 'o'};
    fat32_long_directory_entry_t entry;

    TEST_ASSERT_TRUE(fat32_lfn_make_entry(
        name, 5, 1, 0x42, &entry));

    TEST_ASSERT_EQUAL_HEX8(1 | FAT32_LFN_LAST_ENTRY, entry.order);
    TEST_ASSERT_EQUAL_HEX8(FAT32_ATTR_LONG_NAME, entry.attributes);
    TEST_ASSERT_EQUAL_HEX8(0x42, entry.checksum);
    TEST_ASSERT_EQUAL_UINT16('H', entry.name_1[0]);
    TEST_ASSERT_EQUAL_UINT16('o', entry.name_1[4]);
    TEST_ASSERT_EQUAL_UINT16(0, entry.name_2[0]);
    TEST_ASSERT_EQUAL_UINT16(FAT32_LFN_PADDING, entry.name_2[1]);
}

void test_lfn_multiple_entries(void)
{
    const uint16_t name[] = {
        'M', 'y', ' ', 'F', 'i', 'r', 's', 't', ' ',
        'D', 'o', 'c', 'u', 'm', 'e', 'n', 't',
        '.', 't', 'x', 't'
    };

    fat32_long_directory_entry_t first;
    fat32_long_directory_entry_t second;

    TEST_ASSERT_TRUE(fat32_lfn_make_entry(
        name, 21, 1, 0x42, &first));

    TEST_ASSERT_TRUE(fat32_lfn_make_entry(
        name, 21, 2, 0x42, &second));

    TEST_ASSERT_EQUAL_UINT8(1, first.order);
    TEST_ASSERT_EQUAL_UINT8(2 | FAT32_LFN_LAST_ENTRY, second.order);
    TEST_ASSERT_EQUAL_UINT16('M', first.name_1[0]);
    TEST_ASSERT_EQUAL_UINT16('m', second.name_1[0]);
    TEST_ASSERT_EQUAL_UINT16('t', second.name_2[0]);
    TEST_ASSERT_EQUAL_UINT16('x', second.name_2[1]);
    TEST_ASSERT_EQUAL_UINT16('t', second.name_2[2]);
    TEST_ASSERT_EQUAL_UINT16(0, second.name_2[3]);
    TEST_ASSERT_EQUAL_UINT16(FAT32_LFN_PADDING, second.name_2[4]);
}

void test_lfn_exactly_thirteen_characters(void)
{
    const uint16_t name[] = {
        'A', 'B', 'C', 'D', 'E', 'F', 'G',
        'H', 'I', 'J', 'K', 'L', 'M'
    };

    fat32_long_directory_entry_t entry;

    TEST_ASSERT_TRUE(fat32_lfn_make_entry(
        name, 13, 1, 0x42, &entry));

    TEST_ASSERT_EQUAL_UINT16('M', entry.name_3[1]);
}

void test_lfn_invalid_arguments(void)
{
    const uint16_t name[] = {'A'};
    fat32_long_directory_entry_t entry;

    TEST_ASSERT_FALSE(fat32_lfn_make_entry(
        NULL, 1, 1, 0, &entry));

    TEST_ASSERT_FALSE(fat32_lfn_make_entry(
        name, 1, 1, 0, NULL));

    TEST_ASSERT_FALSE(fat32_lfn_make_entry(
        name, 0, 1, 0, &entry));

    TEST_ASSERT_FALSE(fat32_lfn_make_entry(
        name, 1, 0, 0, &entry));

    TEST_ASSERT_FALSE(fat32_lfn_make_entry(
        name, 1, 2, 0, &entry));

    TEST_ASSERT_FALSE(fat32_lfn_make_entry(
        name, FAT32_LFN_MAX_LENGTH + 1, 1, 0, &entry));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_lfn_single_entry);
    RUN_TEST(test_lfn_multiple_entries);
    RUN_TEST(test_lfn_exactly_thirteen_characters);
    RUN_TEST(test_lfn_invalid_arguments);

    return UNITY_END();
}
