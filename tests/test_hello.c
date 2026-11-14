#include "unity.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_hello_world(void)
{
    TEST_ASSERT_EQUAL_INT(4, 2 + 2);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_hello_world);

    return UNITY_END();
}