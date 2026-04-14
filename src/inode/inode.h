#ifndef INODE_H
#define INODE_H

#include "../common.h"

// Inode structure - must be 4096 bytes total
// Stores FULL locations (segment_id + offset) for each block pointer
struct inode
{
    uint32_t size;                   // File size in bytes (4 bytes)
    uint32_t type;                   // 0 = file, 1 = directory (4 bytes)
    struct location direct[10];      // 10 direct block pointers (10 * 8 = 80 bytes)
    struct location single_indirect; // Single indirect block pointer (8 bytes)
    struct location double_indirect; // Double indirect block pointer (8 bytes)
    struct location triple_indirect; // Triple indirect block pointer (8 bytes)
    uint8_t reserved[3984];          // Padding to make 4096 bytes (4096 - 112 = 3984)
};

// Directory entry - stored in data blocks of directory inodes
struct directory_entry
{
    char name[256];     // Name of file or directory (256 bytes)
    uint32_t inode_num; // Inode number this name points to (4 bytes)
    uint32_t reserved;  // Padding to 264 bytes (for alignment)
};

// Create a new inode
void inode_create(uint32_t *inode_num, uint32_t type);

// Read an inode from disk (uses imap to find current location)
void inode_read(uint32_t inode_num, struct inode *inode);

// Write an inode to disk (appends new version, updates imap)
void inode_write(uint32_t inode_num, struct inode *inode);

// Add an entry to a directory
void dir_add_entry(uint32_t dir_inode, const char *name, uint32_t child_inode);

// Remove an entry from a directory
void dir_remove_entry(uint32_t dir_inode, const char *name);

// Find an entry in a directory, returns inode number or 0 (not found)
uint32_t dir_find_entry(uint32_t dir_inode, const char *name);

// List all entries in a directory with indentation (recursive)
void dir_list_recursive(uint32_t dir_inode, int depth);

// Get the location of a block pointer (handles indirect blocks)
// Returns 1 if block exists, 0 if out of range
int inode_get_block_location(struct inode *inode, uint32_t block_num, struct location *loc);

// Set a block pointer in the inode (handles indirect block creation)
// Returns 1 if successful, 0 if out of range
int inode_set_block_location(struct inode *inode, uint32_t block_num, struct location *loc);

#endif