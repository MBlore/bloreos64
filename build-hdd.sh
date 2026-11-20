#!/bin/bash
set -euo pipefail

IMAGE="image.hdd"
IMAGE_SIZE_MIB=256
PARTITION_START=2048
SECTOR_SIZE=512

# Create a new disk image.
rm -f "$IMAGE"
truncate -s "${IMAGE_SIZE_MIB}M" "$IMAGE"

# Create a GPT partition table containing one EFI System Partition.
/usr/sbin/sgdisk -n "1:${PARTITION_START}:0" -t 1:ef00 "$IMAGE"

# Read the actual partition boundaries from GPT.
PARTITION_INFO=$(/usr/sbin/sgdisk -i 1 "$IMAGE")

PARTITION_FIRST=$(printf '%s\n' "$PARTITION_INFO" | awk '/First sector:/ {print $3}')
PARTITION_LAST=$(printf '%s\n' "$PARTITION_INFO" | awk '/Last sector:/ {print $3}')

# GPT sector ranges are inclusive.
PARTITION_SECTORS=$((PARTITION_LAST - PARTITION_FIRST + 1))
PARTITION_OFFSET=$((PARTITION_FIRST * SECTOR_SIZE))

if (( PARTITION_SECTORS <= 0 )); then
    echo "ERROR: Invalid GPT partition size" >&2
    exit 1
fi

echo "Boot image: ${IMAGE_SIZE_MIB} MiB"
echo "Partition start: ${PARTITION_FIRST}"
echo "Partition end: ${PARTITION_LAST}"
echo "Partition sectors: ${PARTITION_SECTORS}"

# Format exactly the GPT partition as FAT32.
MTOOLS_IMAGE="${IMAGE}@@${PARTITION_OFFSET}"

mformat -F -T "$PARTITION_SECTORS" -i "$MTOOLS_IMAGE" ::

# Create EFI directories.
mmd -i "$MTOOLS_IMAGE" ::/EFI ::/EFI/BOOT

# Install Limine BIOS boot stages.
./limine/limine bios-install "$IMAGE"

# Copy kernel and bootloader resources.
mcopy -n -v -i "$MTOOLS_IMAGE" bin/bloreos limine.cfg limine/limine-bios.sys ::/

# Copy UEFI bootloaders.
mcopy -n -v -i "$MTOOLS_IMAGE" limine/BOOTX64.EFI ::/EFI/BOOT
mcopy -n -v -i "$MTOOLS_IMAGE" limine/BOOTIA32.EFI ::/EFI/BOOT

# Copy custom resources.
mcopy -n -v -i "$MTOOLS_IMAGE" Font.psf ::/

# Install compiled userland executables into /bin.
if [ -d "userland/bin" ]; then
    mmd -i "$MTOOLS_IMAGE" ::/bin

    for executable in userland/bin/*; do
        if [ -f "$executable" ]; then
            mcopy -o -i "$MTOOLS_IMAGE" "$executable" ::/bin/
        fi
    done
fi

echo "FAT32 boot image created successfully."
