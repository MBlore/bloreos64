HOST_CC := gcc
HOST_CFLAGS := -Wall -Wextra -std=gnu11

UNITY_SRC := unity/src/unity.c
INCLUDES := -Iunity/src -I../src/include

# Automatically discover all unit test suites.
# TEST_SRC := $(wildcard test_*.c)

# Execute unit test suites in this order.
TEST_SRC := \
    test_bitmap.c \
    test_mem.c \
    test_vm_translate.c \
    test_vm_map.c \
    test_vm_unmap.c \
    test_vm_pat.c \
    test_vm_mmio.c

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

# Virtual memory manager test dependencies.
VM_SRC := ../src/vm.c
VM_MOCKS := mocks/mock_str.c
VM_HELPERS := vm_test_helpers.c

bin/test_vm_mmio bin/test_vm_pat bin/test_vm_translate bin/test_vm_map bin/test_vm_unmap: bin/%: %.c $(UNITY_SRC) $(VM_SRC) $(VM_MOCKS) $(VM_HELPERS) vm_test_helpers.h mocks/cpu.h
	mkdir -p bin
	$(HOST_CC) $(HOST_CFLAGS) -Imocks $(INCLUDES) \
		-ffreestanding \
		$(UNITY_SRC) $(VM_SRC) $(VM_MOCKS) $(VM_HELPERS) $< \
		-o $@
		
# Compile each test suite into a separate executable.
bin/%: %.c $(UNITY_SRC)
	mkdir -p bin
	$(HOST_CC) $(HOST_CFLAGS) $(INCLUDES) \
		$(UNITY_SRC) $< -o $@

.PHONY: clean
clean:
	rm -rf bin