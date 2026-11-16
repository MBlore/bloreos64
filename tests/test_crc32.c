#include "unity.h"
#include <crc32.h>

void setUp(void)
{
}

void tearDown(void)
{
}

void test_crc32_empty(void)
{
    const char data[] = "";

    TEST_ASSERT_EQUAL_HEX32(
        0x00000000U,
        crc32(data, sizeof(data) - 1)
    );
}

void test_crc32_standard_vector(void)
{
    const char data[] = "123456789";

    TEST_ASSERT_EQUAL_HEX32(
        0xCBF43926U,
        crc32(data, sizeof(data) - 1)
    );
}

void test_crc32_hello(void)
{
    const char data[] = "Hello";

    TEST_ASSERT_EQUAL_HEX32(
        0xF7D18982U,
        crc32(data, sizeof(data) - 1)
    );
}

void test_crc32_lowercase(void)
{
    const char data[] = "hello";

    TEST_ASSERT_EQUAL_HEX32(
        0x3610A686U,
        crc32(data, sizeof(data) - 1)
    );
}

void test_crc32_binary(void)
{
    const uint8_t data[] = {0x00, 0x01, 0x02, 0x03};

    TEST_ASSERT_EQUAL_HEX32(
        0x8BB98613U,
        crc32(data, sizeof(data))
    );
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_crc32_empty);
    RUN_TEST(test_crc32_standard_vector);
    RUN_TEST(test_crc32_hello);
    RUN_TEST(test_crc32_lowercase);
    RUN_TEST(test_crc32_binary);

    return UNITY_END();
}