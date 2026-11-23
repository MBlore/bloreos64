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
#ifndef _BLOREOS_ABI_FCNTL_H
#define _BLOREOS_ABI_FCNTL_H

/*
 * Directory descriptor referring to the process CWD.
 */
#define AT_FDCWD (-100)

/*
 * File access modes.
 */
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2
#define O_ACCMODE 3

/*
 * Require the opened object to be a directory.
 * Matches the Linux x86-64 flag value.
 */
#define O_DIRECTORY 00200000

#endif
