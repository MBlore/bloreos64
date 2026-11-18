![Untitled](https://github.com/user-attachments/assets/5d563803-b389-4bff-803e-4add73b94ad8)

# bloreos64
64-bit operating system from scratch.  
Uses https://github.com/limine-bootloader/limine for bootloading.

init.sh - initializes limine bootloader for building.  
make - builds and creates ISO and HDD images.  
make run - runs QEmu with the built ISO image.
make test - runs the unit tests.

## Dependencies

mtools

xorriso

gdisk

qemu

ovmf

gdb

# Setup

git clone --recurse-submodules https://github.com/MBlore/bloreos64.git
(or run 'git submodule update --init --recursive' if you've already cloned it)

cd bloreos64

mkdir -p logs

mkdir -p iso_root

mkdir -p ovmf

cp /usr/share/OVMF/OVMF_CODE_4M.fd ovmf/OVMF.fd

cp /usr/share/qemu/OVMF.fd ovmf/OVMF.fd

make

make data

make run

## Building the test data disk

Run `make data` to create/refresh the 128 MiB disk image
containing a GPT partition table and FAT32 filesystem.

Files from the `disk-files/` directory are copied
into the filesystem.

Warning: Running this command recreates `data.hdd`
and destroys any existing data on it.

## Development

Compile the kernel:

make


Run QEmu with latest build:

make run

# Project Details

## Technical Decisions

| Component | Technology |
| :---------------- | ------: |
| CPU Arch | x86-64 |
| EXE Format | ELF64 |
| Calling Convention | System V AMD64 ABI |
| Languages | C / LUA |
| Compiler | GCC |
| C Library | mlibc |
| System Calls | SYSCALL / SYSRET |
| Application Priv. | Ring 3 |
| Kernel Priv. | Ring 0 |
| EXE Linking | Static (Dynamic later) |
| POSIX Standards | Practical |

This all allows eventually to take UNIX programs and run them on BloreOS via ports and recompiling.

## Milestones

| # | Milestone | Objective |
| --: | :--- | :--- |
| 1 | FAT32 File System I/O | Read and write persistent files |
| 2 | Virtual File System (VFS) | Filesystem-independent interfaces |
| 3 | Basic Terminal Commands | ls, cat, mkdir, touch, rm |
| 4 | Kernel Memory Management | SLOB and kernel-owned page tables |
| 5 | Networking and Sockets | Ethernet, TCP/IP, UDP and BSD sockets |
| 6 | Process Management | Independent virtual address spaces |
| 7 | User Space | Ring 3 execution and ELF64 loading |
| 8 | System Call Interface | Communication between applications and kernel |
| 9 | Threading and Scheduling | Multitasking and context switching |
| 10 | Multi-Core Support (SMP) | Utilise multiple CPU cores |
| 11 | musl libc Integration | Standard C and POSIX interfaces |
| 12 | Native Application Porting | Run existing open-source UNIX software |
| 13 | Additional Language Support | LUA Scripting |




