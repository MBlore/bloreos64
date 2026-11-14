#include "unity.h"
#include "../src/include/bitmap.h"

#define BITMAP_SIZE 2
#define TEST_BIT 10

void setUp(void)
{
}

void tearDown(void)
{
}

void test_bitmap_on(void)
{
    uint8_t bitmap[BITMAP_SIZE] = {0};

    bitmap_on(bitmap, TEST_BIT);

    TEST_ASSERT_EQUAL_HEX8(0x00, bitmap[0]);
    TEST_ASSERT_EQUAL_HEX8(0x04, bitmap[1]);
}

void test_bitmap_off(void)
{
    uint8_t bitmap[BITMAP_SIZE] = {0xFF, 0xFF};

    bitmap_off(bitmap, TEST_BIT);

    TEST_ASSERT_EQUAL_HEX8(0xFF, bitmap[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFB, bitmap[1]);
}

void test_bitmap_test(void)
{
    uint8_t bitmap[BITMAP_SIZE] = {0x00, 0x04};

    TEST_ASSERT_TRUE(bitmap_test(bitmap, TEST_BIT));
    TEST_ASSERT_FALSE(bitmap_test(bitmap, TEST_BIT - 1));
}

void test_bitmap_byte_boundary(void)
{
    uint8_t bitmap[BITMAP_SIZE] = {0};

    bitmap_on(bitmap, 7);
    bitmap_on(bitmap, 8);

    TEST_ASSERT_EQUAL_HEX8(0x80, bitmap[0]);
    TEST_ASSERT_EQUAL_HEX8(0x01, bitmap[1]);

    bitmap_off(bitmap, 7);

    TEST_ASSERT_EQUAL_HEX8(0x00, bitmap[0]);
    TEST_ASSERT_EQUAL_HEX8(0x01, bitmap[1]);
}

void test_bitmap_all_bits(void)
{
    uint8_t bitmap[BITMAP_SIZE] = {0};

    for (size_t bit = 0; bit < BITMAP_SIZE * 8; bit++)
        bitmap_on(bitmap, bit);

    TEST_ASSERT_EQUAL_HEX8(0xFF, bitmap[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, bitmap[1]);

    for (size_t bit = 0; bit < BITMAP_SIZE * 8; bit++)
        bitmap_off(bitmap, bit);

    TEST_ASSERT_EQUAL_HEX8(0x00, bitmap[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, bitmap[1]);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_bitmap_on);
    RUN_TEST(test_bitmap_off);
    RUN_TEST(test_bitmap_test);
    RUN_TEST(test_bitmap_byte_boundary);
    RUN_TEST(test_bitmap_all_bits);

    return UNITY_END();
}