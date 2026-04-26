/**
 * ExFS-Log: Log-Structured File System
 * * inode.h
 * Defines the structural layout of inodes and directory entries.
 * Enforces strict 4KB (BLOCK_SIZE) alignment for all metadata structures.
 */

#ifndef INODE_H
#define INODE_H

#include "../common.h"

/**
 * struct inode
 * Represents file and directory metadata.
 * Total size must equal exactly 4096 bytes to align with LFS block constraints.
 */
struct inode
{
    uint32_t size;                   // File size in bytes (4 bytes)
    uint32_t type;                   // TYPE_FILE (0) or TYPE_DIRECTORY (1) (4 bytes)
    struct location direct[10];      // 10 direct block pointers (80 bytes)
    struct location single_indirect; // Single indirect block pointer (8 bytes)
    struct location double_indirect; // Double indirect block pointer (8 bytes)
    struct location triple_indirect; // Triple indirect block pointer (8 bytes)
    uint8_t reserved[3984];          // Padding to strictly enforce 4096-byte size
};

/**
 * struct directory_entry
 * Maps human-readable file/directory names to their internal Inode numbers.
 * Total size is exactly 256 bytes, allowing 16 entries per 4KB directory data block.
 */
struct directory_entry
{
    char name[252];     // Name of file or directory
    uint32_t inode_num; // Associated Inode ID
};

void inode_create(uint32_t *inode_num, uint32_t type);
void inode_read(uint32_t inode_num, struct inode *inode);
void inode_write(uint32_t inode_num, struct inode *inode);

void dir_add_entry(uint32_t dir_inode, const char *name, uint32_t child_inode);
void dir_remove_entry(uint32_t dir_inode, const char *name);
uint32_t dir_find_entry(uint32_t dir_inode, const char *name);
void dir_list_recursive(uint32_t dir_inode, int depth);

int inode_get_block_location(struct inode *inode, uint32_t block_num, struct location *loc);
int inode_set_block_location(struct inode *inode, uint32_t block_num, struct location *loc);

#endif