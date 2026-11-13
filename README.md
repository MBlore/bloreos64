![Untitled](https://github.com/user-attachments/assets/5d563803-b389-4bff-803e-4add73b94ad8)

# bloreos64
64-bit operating system from scratch.  
Uses https://github.com/limine-bootloader/limine for bootloading.

init.sh - initializes limine bootloader for building.  
make - builds and creates ISO and HDD images.  
make run - runs QEmu with the built ISO image.

## Dependencies

mtools  
xorriso
gdisk
qemu
ovmf

# Setup

mkdir -p logs
mkdir -p iso_root
mkdir -p ovmf
cp /usr/share/OVMF/OVMF_CODE_4M.fd ovmf/OVMF.fd
cp /usr/share/qemu/OVMF.fd ovmf/OVMF.fd
make
make run