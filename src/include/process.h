/*
    BloreOS - Operating System
    Copyright (C) 2023 Martin Blore

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#ifndef _BLOREOS_PROCESS_H
#define _BLOREOS_PROCESS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <vfs.h>

#define PROCESS_PATH_MAX 1024

typedef uint64_t pid_t;

typedef struct process_page {
    uint64_t virtual_address;
    void *physical_page;
    struct process_page *next;
} process_page_t;

#define PROCESS_MAX_FILES 16

typedef enum {
    PROCESS_FD_NONE = 0,
    PROCESS_FD_FILE,
    PROCESS_FD_DIRECTORY
} process_fd_type_t;

typedef struct process_file {
    process_fd_type_t type;

    union {
        vfs_file_t file;
        vfs_dir_t directory;
    } handle;
} process_file_t;

typedef struct process {
    pid_t pid;
    char cwd[PROCESS_PATH_MAX];
    uint64_t page_table_root;
    uint64_t user_stack_pointer;
    process_page_t *owned_pages;
    process_file_t files[PROCESS_MAX_FILES];
} process_t;

bool            process_alloc_page(process_t *process, uint64_t virt_addr, uint64_t flags);
void            process_free_pages(process_t *process);
void            *process_find_page(const process_t *process, uint64_t virt_addr);

process_t       *process_create(void);
void            process_destroy(process_t *process);

int64_t         process_open(process_t *process, const char *path);
int64_t         process_read(process_t *process, int fd,
                     void *buffer, size_t count);
int64_t         process_close(process_t *process, int fd);
int64_t         process_opendir(process_t *process, const char *path);

os_error_t      process_readdir(process_t *process, int fd,
                           vfs_dirent_t *entry, bool *has_entry);

#endif
