// Filename: common.h
// Contains global constants, system limits, and shared structures used across the entire file system.

#ifndef COMMON_H
#define COMMON_H

#include <stdint.h> // For uint32_t, uint8_t
#include <stdio.h>  // For FILE, size_t
#include <stdlib.h> // For malloc, free, exit
#include <string.h> // For memset, memcpy, strcmp

// File System Dimensions
#define BLOCK_SIZE 4096      // 4KB - standard block size for inodes and data
#define SEGMENT_SIZE 1048576 // 1MB - fixed segment size for log files

// Inode Types
#define TYPE_FILE 0      // Regular file
#define TYPE_DIRECTORY 1 // Directory

// Directory capacity: 4096 / 256 = 16 entries per block
#define DIR_ENTRIES_PER_BLOCK 16

// Indirect block capacity: 4096 / 8 = 512 pointers per block
#define POINTERS_PER_BLOCK 512

// Physical address of any block or inode within the LFS log
struct location
{
    uint32_t segment_id; // Segment file index (0, 1, 2...)
    uint32_t offset;     // Byte offset within that segment
};

#endif