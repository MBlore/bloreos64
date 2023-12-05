# Download the latest Limine binary release.
git clone https://github.com/limine-bootloader/limine.git --branch=v5.x-branch-binary --depth=1
 
# Build limine utility.
make -C limine
 
# Create a directory which will be our ISO root.
mkdir -p iso_root
 
# Copy the relevant files over, including our compiled kernel.
cp -v bin/bloreos limine.cfg limine/limine-bios.sys \
      limine/limine-bios-cd.bin limine/limine-uefi-cd.bin iso_root/
 
# Create the EFI boot tree and copy Limine's EFI executables over.
mkdir -p iso_root/EFI/BOOT
cp -v limine/BOOTX64.EFI iso_root/EFI/BOOT/
cp -v limine/BOOTIA32.EFI iso_root/EFI/BOOT/
