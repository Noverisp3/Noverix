#!/usr/bin/env python3
"""NVFS disk image formatter with directory packing support."""

import os
import struct
import sys

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


def create(path="nvfs_disk.img", src_dir=None):
    disk = bytearray(DISK_SECTORS * SECTOR_SIZE)

    sb = struct.pack(
        "<4sIIIIIIIIIB475x",
        b"NVFS",
        1,  # version
        DISK_SECTORS,  # total_sectors
        BLOCK_SIZE,  # block_size
        BITMAP_START,  # bitmap_start
        BITMAP_SECTORS,  # bitmap_sectors
        INODE_START,  # inode_start
        INODE_COUNT,  # inode_count
        DATA_START,  # data_start
        INODE_SECTORS,  # inode_blocks
        0,  # state = clean
    )
    disk[
        SUPERBLOCK_SECTOR * SECTOR_SIZE : SUPERBLOCK_SECTOR * SECTOR_SIZE + len(sb)
    ] = sb

    files_to_pack = []
    if src_dir and os.path.exists(src_dir):
        for fname in sorted(os.listdir(src_dir)):
            fpath = os.path.join(src_dir, fname)
            if os.path.isfile(fpath):
                with open(fpath, "rb") as f:
                    data = f.read()
                files_to_pack.append((fname, data))

    bitmap_bytes = bytearray(BITMAP_SECTORS * SECTOR_SIZE)
    inodes_bytes = bytearray(INODE_SECTORS * SECTOR_SIZE)

    next_free_block = 0

    if files_to_pack:
        bitmap_bytes[0] |= 1
        next_free_block = 1

        root_entries = bytearray(SECTOR_SIZE)

        for idx, (fname, fdata) in enumerate(files_to_pack):
            if idx >= INODE_COUNT - 1:
                break

            file_inode_num = idx + 1

            name_upper = fname.upper()[:27]
            name_bytes = name_upper.encode("ascii")
            struct.pack_into(
                "<28sI", root_entries, idx * 32, name_bytes, file_inode_num
            )

            num_blocks = (len(fdata) + SECTOR_SIZE - 1) // SECTOR_SIZE
            file_start_block = next_free_block
            next_free_block += num_blocks

            for b in range(file_start_block, next_free_block):
                bitmap_bytes[b // 8] |= 1 << (b % 8)

            disk_offset = (DATA_START + file_start_block) * SECTOR_SIZE
            disk[disk_offset : disk_offset + len(fdata)] = fdata

            extent_count = 1 if num_blocks > 0 else 0
            extents = b"\x00" * 112
            if num_blocks > 0:
                extents = (
                    struct.pack("<II", file_start_block, num_blocks) + b"\x00" * 104
                )

            file_inode = struct.pack(
                "<IB3sI112sI",
                len(fdata),  # size
                1,  # type = NVFS_TYPE_FILE
                b"\x00\x00\x00",  # ctime
                extent_count,  # extent_count
                extents,  # extents
                0,  # mtime
            )
            inodes_bytes[
                file_inode_num * INODE_SIZE : (file_inode_num + 1) * INODE_SIZE
            ] = file_inode

        disk[DATA_START * SECTOR_SIZE : (DATA_START + 1) * SECTOR_SIZE] = root_entries

        root_extents = struct.pack("<II", 0, 1) + b"\x00" * 104
        root_inode = struct.pack(
            "<IB3sI112sI",
            512,  # size (1 block)
            2,  # type = NVFS_TYPE_DIR
            b"\x00\x00\x00",  # ctime
            1,  # extent_count
            root_extents,  # extents
            0,  # mtime
        )
        inodes_bytes[0:INODE_SIZE] = root_inode

    else:
        root_inode = struct.pack(
            "<IB3sI112sI", 0, 2, b"\x00\x00\x00", 0, b"\x00" * 112, 0
        )
        inodes_bytes[0:INODE_SIZE] = root_inode

    disk[BITMAP_START * SECTOR_SIZE : (BITMAP_START + BITMAP_SECTORS) * SECTOR_SIZE] = (
        bitmap_bytes
    )
    disk[INODE_START * SECTOR_SIZE : (INODE_START + INODE_SECTORS) * SECTOR_SIZE] = (
        inodes_bytes
    )

    with open(path, "wb") as f:
        f.write(disk)

    print(f"Created {path} with {len(files_to_pack)} files packed.")


if __name__ == "__main__":
    target_path = sys.argv[1] if len(sys.argv) > 1 else "nvfs_disk.img"
    source_directory = sys.argv[2] if len(sys.argv) > 2 else None
    create(target_path, source_directory)
