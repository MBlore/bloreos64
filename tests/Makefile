
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
    test_vm_mmio.c \
    test_crc32.c \
    test_unicode.c \
    test_str.c \
    test_fat32_lfn.c \
    test_fat32_directory.c \
    test_fat32_dir_create.c \
    test_fat32_dir_delete.c \
    test_slob.c

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

# CRC32 library test dependencies.
CRC32_SRC := ../src/lib/crc32.c

bin/test_crc32: test_crc32.c $(UNITY_SRC) $(CRC32_SRC) ../src/include/crc32.h
	mkdir -p bin
	$(HOST_CC) $(HOST_CFLAGS) $(INCLUDES) \
		$(UNITY_SRC) $(CRC32_SRC) $< -o $@

# Unicode library test dependencies.
UNICODE_SRC := ../src/lib/unicode.c

bin/test_unicode: test_unicode.c $(UNITY_SRC) $(UNICODE_SRC) ../src/include/unicode.h
	mkdir -p bin
	$(HOST_CC) $(HOST_CFLAGS) $(INCLUDES) \
		$(UNITY_SRC) $(UNICODE_SRC) $< -o $@

# String library test dependencies.
STR_SRC := ../src/lib/str.c
STR_MOCKS := mocks/mock_output.c

bin/test_str: test_str.c $(UNITY_SRC) $(STR_SRC) $(STR_MOCKS) ../src/include/str.h
	mkdir -p bin
	$(HOST_CC) $(HOST_CFLAGS) $(INCLUDES) \
		-ffreestanding \
		$(UNITY_SRC) $(STR_SRC) $(STR_MOCKS) $< -o $@

# FAT32 long filename unit tests.
FAT32_LFN_SRC := ../src/fs/fat32/lfn.c
FAT32_UNICODE_SRC := ../src/lib/unicode.c

bin/test_fat32_lfn: test_fat32_lfn.c $(UNITY_SRC) $(FAT32_LFN_SRC) $(FAT32_UNICODE_SRC)
	mkdir -p bin
	$(HOST_CC) $(HOST_CFLAGS) $(INCLUDES) \
		-ffunction-sections -fdata-sections \
		$(UNITY_SRC) $(FAT32_LFN_SRC) $(FAT32_UNICODE_SRC) $< \
		-Wl,--gc-sections -o $@

# FAT32 directory allocation and LFN writer tests.
FAT32_DIRECTORY_SRC := ../src/fs/fat32/directory.c
FAT32_FILE_SRC := ../src/fs/fat32/file.c
FAT32_PATH_SRC := ../src/fs/fat32/path.c
FAT32_UNICODE_SRC := ../src/lib/unicode.c
FAT32_DIRECTORY_MOCKS := mocks/mock_fat32_directory.c
FAT32_TIMESTAMP_MOCK := mocks/mock_fat32_timestamp.c

bin/test_fat32_directory: test_fat32_directory.c $(UNITY_SRC) \
                         $(FAT32_DIRECTORY_SRC) $(FAT32_LFN_SRC) \
                         $(FAT32_FILE_SRC) $(FAT32_PATH_SRC) $(FAT32_UNICODE_SRC) \
                         $(FAT32_DIRECTORY_MOCKS) $(FAT32_TIMESTAMP_MOCK) \
                         mocks/mock_fat32_directory.h
	mkdir -p bin
	$(HOST_CC) $(HOST_CFLAGS) $(INCLUDES) \
		-ffunction-sections -fdata-sections \
		$(UNITY_SRC) $(FAT32_DIRECTORY_SRC) $(FAT32_LFN_SRC) \
		$(FAT32_FILE_SRC) $(FAT32_PATH_SRC) $(FAT32_UNICODE_SRC) \
		$(FAT32_DIRECTORY_MOCKS) $(FAT32_TIMESTAMP_MOCK) $< \
		-Wl,--gc-sections -o $@

# FAT32 directory creation tests.
FAT32_DIR_CREATE_SRC := ../src/fs/fat32/dir_create.c

bin/test_fat32_dir_create: test_fat32_dir_create.c $(UNITY_SRC) \
                           $(FAT32_DIR_CREATE_SRC) $(FAT32_DIRECTORY_SRC) \
                           $(FAT32_LFN_SRC) $(FAT32_UNICODE_SRC) \
                           $(FAT32_PATH_SRC) \
                           $(FAT32_DIRECTORY_MOCKS) $(FAT32_TIMESTAMP_MOCK)
	mkdir -p bin
	$(HOST_CC) $(HOST_CFLAGS) $(INCLUDES) \
		-ffunction-sections -fdata-sections \
		$(UNITY_SRC) $(FAT32_DIR_CREATE_SRC) $(FAT32_DIRECTORY_SRC) \
		$(FAT32_LFN_SRC) $(FAT32_UNICODE_SRC) \
		$(FAT32_PATH_SRC) \
		$(FAT32_DIRECTORY_MOCKS) $(FAT32_TIMESTAMP_MOCK) $< \
		-Wl,--gc-sections -o $@

bin/test_fat32_dir_delete: test_fat32_dir_delete.c $(UNITY_SRC) \
                           $(FAT32_DIR_CREATE_SRC) $(FAT32_DIRECTORY_SRC) \
                           $(FAT32_LFN_SRC) $(FAT32_PATH_SRC) $(FAT32_UNICODE_SRC) \
                           ../src/fs/fat32/dir_delete.c \
                           $(FAT32_DIRECTORY_MOCKS) $(FAT32_TIMESTAMP_MOCK)
	mkdir -p bin
	$(HOST_CC) $(HOST_CFLAGS) $(INCLUDES) \
		-ffunction-sections -fdata-sections \
		$(UNITY_SRC) \
		../src/fs/fat32/dir_delete.c \
		$(FAT32_DIR_CREATE_SRC) \
		$(FAT32_DIRECTORY_SRC) \
		$(FAT32_LFN_SRC) \
		$(FAT32_PATH_SRC) \
		$(FAT32_UNICODE_SRC) \
		$(FAT32_DIRECTORY_MOCKS) $(FAT32_TIMESTAMP_MOCK) $< \
		-Wl,--gc-sections -o $@

# SLOB allocator unit tests.
SLOB_SRC := ../src/alloc/slob.c

bin/test_slob: test_slob.c $(UNITY_SRC) $(SLOB_SRC)
	mkdir -p bin
	$(HOST_CC) $(HOST_CFLAGS) $(INCLUDES) \
		$(UNITY_SRC) $(SLOB_SRC) $< -o $@

.PHONY: clean
clean:
	rm -rf bin
