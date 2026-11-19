#include "unity.h"
#include <str.h>
#include <string.h>

void setUp(void)
{
}

void tearDown(void)
{
}

void test_strcmp_equal(void)
{
    TEST_ASSERT_EQUAL_INT(0, strcmp("hello", "hello"));
}

void test_strcmp_empty(void)
{
    TEST_ASSERT_EQUAL_INT(0, strcmp("", ""));
    TEST_ASSERT_TRUE(strcmp("", "a") < 0);
    TEST_ASSERT_TRUE(strcmp("a", "") > 0);
}

void test_strcmp_prefix(void)
{
    TEST_ASSERT_TRUE(strcmp(".", "..") < 0);
    TEST_ASSERT_TRUE(strcmp("..", ".") > 0);
    TEST_ASSERT_TRUE(strcmp("abc", "abcd") < 0);
    TEST_ASSERT_TRUE(strcmp("abcd", "abc") > 0);
}

void test_strcmp_order(void)
{
    TEST_ASSERT_TRUE(strcmp("abc", "abd") < 0);
    TEST_ASSERT_TRUE(strcmp("abd", "abc") > 0);
}

void test_strcmp_case_sensitive(void)
{
    TEST_ASSERT_TRUE(strcmp("hello", "HELLO") != 0);
}

void test_strcmp_unsigned(void)
{
    TEST_ASSERT_TRUE(strcmp("\xFF", "\x01") > 0);
}

void test_strcasecmp_equal(void)
{
    TEST_ASSERT_EQUAL_INT(0, strcasecmp("hello", "HELLO"));
    TEST_ASSERT_EQUAL_INT(0, strcasecmp("Controller.c", "controller.C"));
}

void test_strcasecmp_empty(void)
{
    TEST_ASSERT_EQUAL_INT(0, strcasecmp("", ""));
    TEST_ASSERT_TRUE(strcasecmp("", "A") < 0);
    TEST_ASSERT_TRUE(strcasecmp("A", "") > 0);
}

void test_strcasecmp_prefix(void)
{
    TEST_ASSERT_TRUE(strcasecmp("ABC", "abcd") < 0);
    TEST_ASSERT_TRUE(strcasecmp("ABCD", "abc") > 0);
}

void test_strcasecmp_order(void)
{
    TEST_ASSERT_TRUE(strcasecmp("ABC", "abd") < 0);
    TEST_ASSERT_TRUE(strcasecmp("ABD", "abc") > 0);
}

/*
 * Verifies that snprintf truncates safely while returning
 * the full length of the formatted output.
 */
void test_snprintf_truncation(void)
{
    char buffer[5];

    int result = snprintf(buffer, sizeof(buffer), "%s", "HELLOWORLD");

    TEST_ASSERT_EQUAL_INT(10, result);
    TEST_ASSERT_EQUAL_STRING("HELL", buffer);
}

/*
 * Verifies the minimum signed integer values are formatted
 * correctly without signed overflow.
 */
void test_snprintf_signed_limits(void)
{
    char buffer[64];

    snprintf(buffer, sizeof(buffer), "%d", INT_MIN);
    TEST_ASSERT_EQUAL_STRING("-2147483648", buffer);

    snprintf(buffer, sizeof(buffer), "%ld", LONG_MIN);
    TEST_ASSERT_EQUAL_STRING("-9223372036854775808", buffer);
}

/*
 * Verifies that unsigned formatting supports the full
 * 32-bit unsigned integer range.
 */
void test_snprintf_unsigned_limits(void)
{
    char buffer[64];

    int result = snprintf(buffer, sizeof(buffer), "%u", UINT32_MAX);

    TEST_ASSERT_EQUAL_INT(10, result);
    TEST_ASSERT_EQUAL_STRING("4294967295", buffer);
}

/*
 * Verifies that a zero-sized destination is not written to
 * and the required output length is still returned.
 */
void test_snprintf_zero_size(void)
{
    char buffer = 'X';

    int result = snprintf(&buffer, 0, "%s", "HELLO");

    TEST_ASSERT_EQUAL_INT(5, result);
    TEST_ASSERT_EQUAL_CHAR('X', buffer);
}

/*
 * Verifies that escaped percent signs are emitted literally
 * without consuming additional formatting arguments.
 */
void test_snprintf_percent_escape(void)
{
    char buffer[64];

    int result = snprintf(buffer, sizeof(buffer), "Progress: 100%% complete");

    TEST_ASSERT_EQUAL_INT(23, result);
    TEST_ASSERT_EQUAL_STRING("Progress: 100% complete", buffer);
}

/*
 * Verifies that formatting output larger than the destination
 * remains bounded and reports the full required length.
 */
void test_snprintf_large_output(void)
{
    struct {
        char buffer[8];
        char guard;
    } output;

    memset(&output, 0, sizeof(output));
    output.guard = 'X';

    int result = snprintf(output.buffer, sizeof(output.buffer),
                          "%u-%u-%u", UINT32_MAX, UINT32_MAX, UINT32_MAX);

    TEST_ASSERT_EQUAL_INT(32, result);
    TEST_ASSERT_EQUAL_STRING("4294967", output.buffer);
    TEST_ASSERT_EQUAL_CHAR('X', output.guard);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_strcmp_equal);
    RUN_TEST(test_strcmp_empty);
    RUN_TEST(test_strcmp_prefix);
    RUN_TEST(test_strcmp_order);
    RUN_TEST(test_strcmp_case_sensitive);
    RUN_TEST(test_strcmp_unsigned);
    RUN_TEST(test_strcasecmp_equal);
    RUN_TEST(test_strcasecmp_empty);
    RUN_TEST(test_strcasecmp_prefix);
    RUN_TEST(test_strcasecmp_order);
    RUN_TEST(test_snprintf_truncation);
    RUN_TEST(test_snprintf_signed_limits);
    RUN_TEST(test_snprintf_unsigned_limits);
    RUN_TEST(test_snprintf_zero_size);
    RUN_TEST(test_snprintf_percent_escape);
    RUN_TEST(test_snprintf_large_output);

    return UNITY_END();
}