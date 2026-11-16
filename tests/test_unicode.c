#include "unity.h"
#include <unicode.h>

void setUp(void)
{
}

void tearDown(void)
{
}

void test_unicode_empty(void)
{
    const uint16_t source[] = {0};
    char destination[1];

    TEST_ASSERT_TRUE(utf16_to_utf8(source, 0, destination, sizeof(destination)));
    TEST_ASSERT_EQUAL_STRING("", destination);
}

void test_unicode_ascii(void)
{
    const uint16_t source[] = {'h', 'e', 'l', 'l', 'o'};
    char destination[6];

    TEST_ASSERT_TRUE(utf16_to_utf8(source, 5, destination, sizeof(destination)));
    TEST_ASSERT_EQUAL_STRING("hello", destination);
}

void test_unicode_two_bytes(void)
{
    const uint16_t source[] = {0x00E9};
    char destination[3];

    TEST_ASSERT_TRUE(utf16_to_utf8(source, 1, destination, sizeof(destination)));
    TEST_ASSERT_EQUAL_STRING("\xC3\xA9", destination);
}

void test_unicode_three_bytes(void)
{
    const uint16_t source[] = {0x4E2D};
    char destination[4];

    TEST_ASSERT_TRUE(utf16_to_utf8(source, 1, destination, sizeof(destination)));
    TEST_ASSERT_EQUAL_STRING("\xE4\xB8\xAD", destination);
}

void test_unicode_surrogate_pair(void)
{
    const uint16_t source[] = {0xD83D, 0xDE00};
    char destination[5];

    TEST_ASSERT_TRUE(utf16_to_utf8(source, 2, destination, sizeof(destination)));
    TEST_ASSERT_EQUAL_STRING("\xF0\x9F\x98\x80", destination);
}

void test_unicode_missing_low_surrogate(void)
{
    const uint16_t source[] = {0xD83D};
    char destination[5];

    TEST_ASSERT_FALSE(utf16_to_utf8(source, 1, destination, sizeof(destination)));
}

void test_unicode_unexpected_low_surrogate(void)
{
    const uint16_t source[] = {0xDE00};
    char destination[5];

    TEST_ASSERT_FALSE(utf16_to_utf8(source, 1, destination, sizeof(destination)));
}

void test_unicode_invalid_surrogate_pair(void)
{
    const uint16_t source[] = {0xD83D, 'A'};
    char destination[5];

    TEST_ASSERT_FALSE(utf16_to_utf8(source, 2, destination, sizeof(destination)));
}

void test_unicode_insufficient_capacity(void)
{
    const uint16_t source[] = {0xD83D, 0xDE00};
    char destination[4];

    TEST_ASSERT_FALSE(utf16_to_utf8(source, 2, destination, sizeof(destination)));
}

void test_unicode_invalid_arguments(void)
{
    const uint16_t source[] = {'A'};
    char destination[2];

    TEST_ASSERT_FALSE(utf16_to_utf8(NULL, 1, destination, sizeof(destination)));
    TEST_ASSERT_FALSE(utf16_to_utf8(source, 1, NULL, sizeof(destination)));
    TEST_ASSERT_FALSE(utf16_to_utf8(source, 1, destination, 0));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_unicode_empty);
    RUN_TEST(test_unicode_ascii);
    RUN_TEST(test_unicode_two_bytes);
    RUN_TEST(test_unicode_three_bytes);
    RUN_TEST(test_unicode_surrogate_pair);
    RUN_TEST(test_unicode_missing_low_surrogate);
    RUN_TEST(test_unicode_unexpected_low_surrogate);
    RUN_TEST(test_unicode_invalid_surrogate_pair);
    RUN_TEST(test_unicode_insufficient_capacity);
    RUN_TEST(test_unicode_invalid_arguments);

    return UNITY_END();
}