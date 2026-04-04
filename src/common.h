#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Block and segment sizes (requirements)
#define BLOCK_SIZE 4096      // 4KB blocks and inodes
#define SEGMENT_SIZE 1048576 // 1MB segments

// Inode type constants
#define TYPE_FILE 0
#define TYPE_DIRECTORY 1

// Maximum entries per directory (BLOCK_SIZE / sizeof(struct directory_entry))
#define DIR_ENTRIES_PER_BLOCK 16

// Pointers per indirect block (BLOCK_SIZE / sizeof(struct location))
#define POINTERS_PER_BLOCK 512 // 4096 / 8 = 512

// Location structure - where something lives in the log
struct location
{
    uint32_t segment_id; // Which segment file (0, 1, 2...)
    uint32_t offset;     // Byte offset within that segment
};

#endif