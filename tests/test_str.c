#include "unity.h"
#include <str.h>

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

    return UNITY_END();
}