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