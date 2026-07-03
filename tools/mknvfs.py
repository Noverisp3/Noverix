#!/usr/bin/env python3
"""NVFS disk image formatter."""

import struct, sys

SECTOR_SIZE = 512
DISK_SECTORS = 32768
BLOCK_SIZE = 1

BITMAP_SECTORS = 8
INODE_COUNT = 128
INODE_SIZE = 128
INODE_SECTORS = (INODE_COUNT * INODE_SIZE + SECTOR_SIZE - 1) // SECTOR_SIZE

BOOT_SECTOR = 0
SUPERBLOCK_SECTOR = 1
BITMAP_START = 2
INODE_START = BITMAP_START + BITMAP_SECTORS
DATA_START = INODE_START + INODE_SECTORS
DATA_BLOCKS = DISK_SECTORS - DATA_START

def create(path="nvfs_disk.img"):
    disk = bytearray(DISK_SECTORS * SECTOR_SIZE)

    # Superblock at sector 1
    sb = struct.pack('<4sIIIIIIIIB475x',
        b'NVFS',
        1,                     # version
        DISK_SECTORS,          # total_sectors
        BLOCK_SIZE,            # block_size
        BITMAP_START,          # bitmap_start
        BITMAP_SECTORS,        # bitmap_sectors
        INODE_START,           # inode_start
        INODE_COUNT,           # inode_count
        DATA_START,            # data_start
        0,                     # state = clean
    )
    disk[SUPERBLOCK_SECTOR * SECTOR_SIZE : SUPERBLOCK_SECTOR * SECTOR_SIZE + len(sb)] = sb

    # Bitmap at sectors 2-9: all zeros (all data blocks free)

    # Inode table at sectors 10-41
    # Root inode (inode 0): directory, size 0, no extents
    inode0 = struct.pack('<IB3xI112s4x',
        0,     # size
        2,     # type = directory
        0,     # extent_count = 0
        b'\x00' * 112,
    )
    inode_offset = INODE_START * SECTOR_SIZE
    disk[inode_offset : inode_offset + INODE_SIZE] = inode0

    with open(path, 'wb') as f:
        f.write(disk)

    print(f"Created {path}")
    print(f"  Sectors: {DISK_SECTORS} ({DISK_SECTORS * SECTOR_SIZE // (1024*1024)} MB)")
    print(f"  Bitmap: sector {BITMAP_START}-{BITMAP_START + BITMAP_SECTORS - 1} ({BITMAP_SECTORS} sectors)")
    print(f"  Inodes: sector {INODE_START}-{INODE_START + INODE_SECTORS - 1} ({INODE_COUNT} inodes)")
    print(f"  Data:   sector {DATA_START}-{DISK_SECTORS - 1} ({DATA_BLOCKS} blocks)")
    print(f"  Root:   inode 0")

if __name__ == '__main__':
    create(sys.argv[1] if len(sys.argv) > 1 else "nvfs_disk.img")
