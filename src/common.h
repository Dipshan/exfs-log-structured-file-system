/**
 * ExFS-Log: Log-Structured File System
 * * common.h
 * Contains global constants, system limits, and shared structures 
 * utilized across the entire file system architecture.
 */

#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- File System Dimensions ---
#define BLOCK_SIZE 4096      // Standard 4KB blocks for inodes and data
#define SEGMENT_SIZE 1048576 // 1MB segments for log appending

// --- Inode Configuration ---
#define TYPE_FILE 0
#define TYPE_DIRECTORY 1

// Maximum entries per directory block (BLOCK_SIZE / sizeof(struct directory_entry))
#define DIR_ENTRIES_PER_BLOCK 16

// Pointers per indirect block (BLOCK_SIZE / sizeof(struct location))
#define POINTERS_PER_BLOCK 512

/**
 * struct location
 * Represents the physical address of any block or inode within the LFS.
 */
struct location
{
    uint32_t segment_id; // The specific segment file (e.g., segment0.bin)
    uint32_t offset;     // The byte offset within that segment
};

#endif