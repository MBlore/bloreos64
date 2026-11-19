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

#include <commands.h>
#include <terminal.h>
#include <vfs.h>
#include <error.h>
#include <str.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <rtc.h>

#define COMMAND_MAX_ARGS 8
#define COMMAND_READ_SIZE 128
#define COMMAND_PATH_MAX 1024

static char current_directory[COMMAND_PATH_MAX] = "/";

typedef void (*command_handler_t)(int argc, char **argv);

typedef struct {
    const char *name;
    command_handler_t handler;
} command_t;

static void command_ls(int argc, char **argv);
static void command_cat(int argc, char **argv);
static void command_mkdir(int argc, char **argv);
static void command_rmdir(int argc, char **argv);
static void command_touch(int argc, char **argv);
static void command_rm(int argc, char **argv);
static void command_help(int argc, char **argv);
static void command_pwd(int argc, char **argv);
static void command_cd(int argc, char **argv);
static void command_date(int argc, char **argv);
static void command_uptime(int argc, char **argv);
static bool command_get_path(const char *name, const char *argument, char *path, size_t capacity);

/*
 * Registers the filesystem command names and their handlers.
 */
static const command_t commands[] = {
    { "ls", command_ls },
    { "cat", command_cat },
    { "mkdir", command_mkdir },
    { "rmdir", command_rmdir },
    { "touch", command_touch },
    { "rm", command_rm },
    { "help", command_help },
    { "pwd", command_pwd },
    { "cd", command_cd },
    { "date", command_date },
    { "uptime", command_uptime },
};

/*
 * Lists the available filesystem commands.
 */
static void command_help(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    tprintf("Filesystem commands:\n");
    tprintf("  pwd             Print working directory\n");
    tprintf("  cd [path]       Change directory\n");
    tprintf("  ls [-l] [path]  List directory (long format shows modification date)\n");
    tprintf("  cat <path>      Print file contents\n");
    tprintf("  mkdir <path>    Create directory\n");
    tprintf("  rmdir <path>    Remove empty directory\n");
    tprintf("  touch <path>    Create file if missing\n");
    tprintf("  rm <path>       Remove file\n");
    tprintf("  date            Show current UTC date and time\n");
    tprintf("  uptime          Show time since boot\n");
}

/*
 * Prints a filesystem error using the existing OS error descriptions.
 */
static void command_error(const char *name, os_error_t result)
{
    tprintf("%s: %s\n", name, os_error_string(result));
}

/*
 * Prints a VFS calendar timestamp as YYYY-MM-DD HH:MM.
 *
 * Unknown timestamps display a placeholder. FAT32 modification
 * times have two-second resolution, so seconds are omitted.
 */
static void command_print_timestamp(const vfs_datetime_t *dt)
{
    if (!dt->valid || !dt->has_time) {
        tprintf("---- -- -- --:--");
        return;
    }

    tprintf("%u-%c%c-%c%c %c%c:%c%c",
            (unsigned int)dt->year,
            '0' + dt->month / 10, '0' + dt->month % 10,
            '0' + dt->day / 10, '0' + dt->day % 10,
            '0' + dt->hour / 10, '0' + dt->hour % 10,
            '0' + dt->minute / 10, '0' + dt->minute % 10);
}

/*
 * Displays one directory entry in normal or long-listing format.
 *
 * The callback context points to the long-format flag.
 */
static os_error_t command_ls_entry(const vfs_dirent_t *entry, void *context)
{
    bool long_format = *(const bool *)context;

    if (long_format) {
        command_print_timestamp(&entry->modified);
        tprintf("  ");
    }

    if (entry->is_directory) {
        tprintf("<DIR>  %s\n", entry->name);
    } else {
        tprintf("       %s (%lu bytes)\n", entry->name, (unsigned long)entry->size);
    }

    return OS_OK;
}

/*
 * Lists a directory in normal or long format.
 *
 * Supported forms:
 *   ls
 *   ls <path>
 *   ls -l
 *   ls -l <path>
 */
static void command_ls(int argc, char **argv)
{
    bool long_format = false;
    const char *requested = ".";

    if (argc >= 2 && strcmp(argv[1], "-l") == 0) {
        long_format = true;

        if (argc == 3) {
            requested = argv[2];
        } else if (argc > 3) {
            tprintf("Usage: ls [-l] [path]\n");
            return;
        }
    } else if (argc == 2) {
        requested = argv[1];
    } else if (argc > 2) {
        tprintf("Usage: ls [-l] [path]\n");
        return;
    }

    char path[COMMAND_PATH_MAX];

    if (!command_get_path("ls", requested, path, sizeof(path))) {
        return;
    }

    os_error_t result = vfs_listdir(path, command_ls_entry, &long_format);

    if (result != OS_OK) {
        command_error("ls", result);
    }
}

/*
 * Prints a regular file's contents using a resolved VFS path.
 */
static void command_cat(int argc, char **argv)
{
    if (argc != 2) {
        tprintf("Usage: cat <path>\n");
        return;
    }

    char path[COMMAND_PATH_MAX];

    if (!command_get_path("cat", argv[1], path, sizeof(path))) {
        return;
    }

    vfs_file_t file = {0};
    os_error_t result = vfs_open(path, VFS_OPEN_READ, &file);

    if (result != OS_OK) {
        command_error("cat", result);
        return;
    }

    char buffer[COMMAND_READ_SIZE + 1];

    while (true) {
        int64_t count = vfs_read(&file, buffer, COMMAND_READ_SIZE);

        if (count < 0) {
            tprintf("cat: Read failed\n");
            break;
        }

        if (count == 0) {
            break;
        }

        buffer[count] = '\0';
        tprintf("%s", buffer);
    }

    vfs_close(&file);
    tprintf("\n");
}

/*
 * Creates a directory through the VFS.
 */
static void command_mkdir(int argc, char **argv)
{
    if (argc != 2) {
        tprintf("Usage: mkdir <path>\n");
        return;
    }

    char path[COMMAND_PATH_MAX];

    if (!command_get_path("mkdir", argv[1], path, sizeof(path))) {
        return;
    }

    os_error_t result = vfs_mkdir(path);

    if (result != OS_OK) {
        command_error("mkdir", result);
    }
}

/*
 * Removes an empty directory through the VFS.
 */
static void command_rmdir(int argc, char **argv)
{
    if (argc != 2) {
        tprintf("Usage: rmdir <path>\n");
        return;
    }

    char path[COMMAND_PATH_MAX];

    if (!command_get_path("rmdir", argv[1], path, sizeof(path))) {
        return;
    }

    os_error_t result = vfs_rmdir(path);

    if (result != OS_OK) {
        command_error("rmdir", result);
    }
}

/*
 * Creates an empty file if it does not already exist.
 *
 * Preserves existing file contents.
 */
static void command_touch(int argc, char **argv)
{
    if (argc != 2) {
        tprintf("Usage: touch <path>\n");
        return;
    }

    char path[COMMAND_PATH_MAX];

    if (!command_get_path("touch", argv[1], path, sizeof(path))) {
        return;
    }

    vfs_file_t file = {0};
    os_error_t result = vfs_open(path, VFS_OPEN_WRITE | VFS_OPEN_CREATE, &file);

    if (result != OS_OK) {
        command_error("touch", result);
        return;
    }

    vfs_close(&file);
}

/*
 * Removes a regular file through the VFS.
 */
static void command_rm(int argc, char **argv)
{
    if (argc != 2) {
        tprintf("Usage: rm <path>\n");
        return;
    }

    char path[COMMAND_PATH_MAX];

    if (!command_get_path("rm", argv[1], path, sizeof(path))) {
        return;
    }

    os_error_t result = vfs_unlink(path);

    if (result != OS_OK) {
        command_error("rm", result);
    }
}

/*
 * Parses an input line into whitespace-delimited arguments.
 *
 * Modifies the input buffer in place. Quoted arguments and escaped
 * spaces are not supported yet.
 */
static int command_parse(char *line, char **argv, int capacity)
{
    int argc = 0;
    char *cursor = line;

    while (*cursor != '\0') {
        while (*cursor == ' ' || *cursor == '\t') {
            cursor++;
        }

        if (*cursor == '\0') {
            break;
        }

        if (argc == capacity) {
            return -1;
        }

        argv[argc++] = cursor;

        while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t') {
            cursor++;
        }

        if (*cursor != '\0') {
            *cursor++ = '\0';
        }
    }

    return argc;
}

/*
 * Executes a registered filesystem command.
 *
 * Returns true when the command is recognised, including when it
 * reports a usage or filesystem error. Returns false for unknown
 * commands so the terminal can display its existing fallback.
 */
bool commands_execute(char *line)
{
    if (line == NULL) {
        return false;
    }

    char *argv[COMMAND_MAX_ARGS];
    int argc = command_parse(line, argv, COMMAND_MAX_ARGS);

    if (argc < 0) {
        tprintf("Too many arguments\n");
        return true;
    }

    if (argc == 0) {
        return true;
    }

    size_t count = sizeof(commands) / sizeof(commands[0]);

    for (size_t i = 0; i < count; i++) {
        if (strcmp(argv[0], commands[i].name) == 0) {
            commands[i].handler(argc, argv);
            return true;
        }
    }

    return false;
}

/*
 * Removes the final component of a normalised absolute path.
 *
 * The root directory is its own parent.
 */
static void command_path_parent(char *path, size_t *length)
{
    if (*length <= 1) {
        return;
    }

    while (*length > 1 && path[*length - 1] != '/') {
        (*length)--;
    }

    if (*length > 1) {
        (*length)--;
    }

    path[*length] = '\0';
}

/*
 * Converts an absolute or relative path into a normalised absolute path.
 *
 * Resolves "." and "..", collapses repeated separators and removes
 * trailing separators except for the root directory.
 *
 * Returns OS_ERR_NAME_TOO_LONG if the output buffer is insufficient.
 * Does not access the filesystem; existence is checked by the caller.
 */
static os_error_t command_resolve_path(const char *input, char *output, size_t capacity)
{
    if (input == NULL || output == NULL || capacity < 2 || input[0] == '\0') {
        return OS_ERR_INVALID_ARGUMENT;
    }

    size_t length = 1;
    output[0] = '/';
    output[1] = '\0';

    if (input[0] != '/') {
        size_t cwd_length = strlen(current_directory);

        if (cwd_length >= capacity) {
            return OS_ERR_NAME_TOO_LONG;
        }

        for (size_t i = 0; i <= cwd_length; i++) {
            output[i] = current_directory[i];
        }

        length = cwd_length;
    }

    const char *cursor = input;

    while (*cursor != '\0') {
        while (*cursor == '/') {
            cursor++;
        }

        if (*cursor == '\0') {
            break;
        }

        const char *component = cursor;

        while (*cursor != '\0' && *cursor != '/') {
            cursor++;
        }

        size_t component_length = (size_t)(cursor - component);

        if (component_length == 1 && component[0] == '.') {
            continue;
        }

        if (component_length == 2 && component[0] == '.' && component[1] == '.') {
            command_path_parent(output, &length);
            continue;
        }

        size_t separator_length = length > 1 ? 1 : 0;

        if (component_length >= capacity - length - separator_length) {
            return OS_ERR_NAME_TOO_LONG;
        }

        if (separator_length != 0) {
            output[length++] = '/';
        }

        for (size_t i = 0; i < component_length; i++) {
            output[length++] = component[i];
        }

        output[length] = '\0';
    }

    return OS_OK;
}

/*
 * Prints the terminal's current working directory.
 */
static void command_pwd(int argc, char **argv)
{
    (void)argv;

    if (argc != 1) {
        tprintf("Usage: pwd\n");
        return;
    }

    tprintf("%s\n", current_directory);
}

/*
 * Changes the current working directory.
 *
 * Resolves the requested path and verifies that it identifies an
 * existing directory before updating the terminal's working directory.
 *
 * With no arguments, returns to the root directory.
 */
static void command_cd(int argc, char **argv)
{
    if (argc > 2) {
        tprintf("Usage: cd [path]\n");
        return;
    }

    const char *requested = argc == 1 ? "/" : argv[1];

    char path[COMMAND_PATH_MAX];
    os_error_t result = command_resolve_path(requested, path, sizeof(path));

    if (result != OS_OK) {
        command_error("cd", result);
        return;
    }

    vfs_stat_t stat;
    result = vfs_stat(path, &stat);

    if (result != OS_OK) {
        command_error("cd", result);
        return;
    }

    if (stat.type != VFS_TYPE_DIRECTORY) {
        tprintf("cd: Not a directory\n");
        return;
    }

    size_t length = strlen(path);

    for (size_t i = 0; i <= length; i++) {
        current_directory[i] = path[i];
    }
}

/*
 * Resolves a command's path argument and reports errors to the terminal.
 *
 * Returns true if the result is a usable absolute VFS path.
 */
static bool command_get_path(const char *name, const char *argument, char *path, size_t capacity)
{
    os_error_t result = command_resolve_path(argument, path, capacity);

    if (result != OS_OK) {
        command_error(name, result);
        return false;
    }

    return true;
}

/*
 * Returns the terminal's current working directory.
 *
 * The returned pointer refers to shell-owned storage and must not
 * be modified or freed by the caller.
 */
const char *commands_get_cwd(void)
{
    return current_directory;
}

/*
 * Displays the current system date and time in UTC.
 * Uses the software clock rather than rereading the RTC.
 */
static void command_date(int argc, char **argv)
{
    (void)argv;

    if (argc != 1) {
        tprintf("Usage: date\n");
        return;
    }

    rtc_datetime_t now;

    if (!time_get_datetime(&now)) {
        tprintf("date: System clock unavailable\n");
        return;
    }

    tprintf("%u-%c%c-%c%c %c%c:%c%c:%c%c UTC\n",
            (unsigned int)now.year,
            '0' + now.month / 10, '0' + now.month % 10,
            '0' + now.day / 10, '0' + now.day % 10,
            '0' + now.hour / 10, '0' + now.hour % 10,
            '0' + now.minute / 10, '0' + now.minute % 10,
            '0' + now.second / 10, '0' + now.second % 10);
}

/*
 * Displays monotonic kernel uptime as hours, minutes and seconds.
 */
static void command_uptime(int argc, char **argv)
{
    (void)argv;

    if (argc != 1) {
        tprintf("Usage: uptime\n");
        return;
    }

    uint64_t seconds = time_get_uptime_ms() / 1000;
    uint64_t hours = seconds / 3600;
    uint64_t minutes = (seconds % 3600) / 60;
    uint64_t remaining = seconds % 60;

    tprintf("Uptime: %lu hours, %lu minutes, %lu seconds\n",
            (unsigned long)hours, (unsigned long)minutes, (unsigned long)remaining);
}