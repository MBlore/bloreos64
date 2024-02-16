# Copy the relevant files over, including our compiled kernel.
cp -v bin/bloreos limine.cfg limine/limine-bios.sys \
      limine/limine-bios-cd.bin limine/limine-uefi-cd.bin \
      Font.psf iso_root/
 
# Create the bootable ISO.
xorriso -as mkisofs -b limine-bios-cd.bin \
        -no-emul-boot -boot-load-size 4 -boot-info-table \
        --efi-boot limine-uefi-cd.bin \
        -efi-boot-part --efi-boot-image --protective-msdos-label \
        iso_root -o image.iso
 
# Install Limine stage 1 and 2 for legacy BIOS boot.
./limine/limine bios-install image.iso