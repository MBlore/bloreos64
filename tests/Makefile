HOST_CC := gcc
TEST_BINARY := bin/unit_tests

UNITY_SRC := unity/src/unity.c
TEST_SRC := test_hello.c

.PHONY: all
all:
	mkdir -p bin
	$(HOST_CC) -Wall -Wextra -std=gnu11 \
		-Iunity/src \
		$(UNITY_SRC) $(TEST_SRC) \
		-o $(TEST_BINARY)
	./$(TEST_BINARY)

.PHONY: clean
clean:
	rm -rf bin