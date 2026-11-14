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

# Memory manager test dependencies.
MEM_SRC := ../src/mem.c
MEM_MOCKS := mocks/mock_str.c

bin/test_mem: test_mem.c $(UNITY_SRC) $(MEM_SRC) $(MEM_MOCKS) mocks/kernel.h
	mkdir -p bin
	$(HOST_CC) $(HOST_CFLAGS) $(INCLUDES) \
		-Imocks \
		-ffreestanding \
		$(UNITY_SRC) $(MEM_SRC) $(MEM_MOCKS) $< \
		-o $@
		
# Compile each test suite into a separate executable.
bin/%: %.c $(UNITY_SRC)
	mkdir -p bin
	$(HOST_CC) $(HOST_CFLAGS) $(INCLUDES) \
		$(UNITY_SRC) $< -o $@

.PHONY: clean
clean:
	rm -rf bin