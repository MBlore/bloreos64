HOST_CC := gcc
HOST_CFLAGS := -Wall -Wextra -std=gnu11

UNITY_SRC := unity/src/unity.c
INCLUDES := -Iunity/src -I../src/include

# Automatically discover all unit test suites.
TEST_SRC := $(wildcard test_*.c)
TEST_BIN := $(patsubst %.c,bin/%,$(TEST_SRC))

# Build and execute all unit test suites.
.PHONY: all
all: $(TEST_BIN)
	@set -e; for test in $(TEST_BIN); do \
		./$$test; \
	done

# Compile each test suite into a separate executable.
bin/%: %.c $(UNITY_SRC)
	mkdir -p bin
	$(HOST_CC) $(HOST_CFLAGS) $(INCLUDES) \
		$(UNITY_SRC) $< -o $@

.PHONY: clean
clean:
	rm -rf bin