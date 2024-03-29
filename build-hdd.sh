# Create an empty zeroed-out 64MiB image file.
rm -f image.hdd
dd if=/dev/zero bs=1M count=0 seek=64 of=image.hdd
 
# Create a GPT partition table.
/usr/sbin/sgdisk image.hdd -n 1:2048 -t 1:ef00
 
# Format the image as fat32.
mformat -i image.hdd@@1M
 
# Make /EFI and /EFI/BOOT an MSDOS subdirectory.
mmd -i image.hdd@@1M ::/EFI ::/EFI/BOOT

# Install the Limine BIOS stages onto the image.
./limine/limine bios-install image.hdd
 
# Copy over the relevant files
mcopy -n -v -i image.hdd@@1M bin/bloreos limine.cfg limine/limine-bios.sys ::/
mcopy -n -v -i image.hdd@@1M limine/BOOTX64.EFI ::/EFI/BOOT
mcopy -n -v -i image.hdd@@1M limine/BOOTIA32.EFI ::/EFI/BOOT

# Copy over custom files
mcopy -n -v -i image.hdd@@1M Font.psf ::/