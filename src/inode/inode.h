// Filename: inode.h
// Defines the layout of inodes and directory entries.
// Enforces strict 4KB alignment for all metadata structures.

#ifndef INODE_H
#define INODE_H

#include "../common.h" // For BLOCK_SIZE, location struct, TYPE_FILE, TYPE_DIRECTORY

// Inode structure - must be exactly 4096 bytes
struct inode
{
    uint32_t size;                   // File size in bytes
    uint32_t type;                   // 0 = file, 1 = directory
    struct location direct[10];      // 10 direct block pointers
    struct location single_indirect; // Single indirect block pointer
    struct location double_indirect; // Double indirect block pointer
    struct location triple_indirect; // Triple indirect block pointer
    uint8_t reserved[3984];          // Padding to 4096 bytes
};

// Directory entry - 256 bytes, 16 entries per 4KB block
struct directory_entry
{
    char name[252];     // File or directory name
    uint32_t inode_num; // Inode number this name points to
};

// Creates a new inode (file or directory)
void inode_create(uint32_t *inode_num, uint32_t type);

// Reads an inode from disk using imap lookup
void inode_read(uint32_t inode_num, struct inode *inode);

// Writes an inode to disk (appends new version)
void inode_write(uint32_t inode_num, struct inode *inode);

// Adds a directory entry (name -> inode)
void dir_add_entry(uint32_t dir_inode, const char *name, uint32_t child_inode);

// Removes a directory entry by name
void dir_remove_entry(uint32_t dir_inode, const char *name);

// Finds an entry by name, returns inode number or 0
uint32_t dir_find_entry(uint32_t dir_inode, const char *name);

// Recursively prints directory tree with indentation
void dir_list_recursive(uint32_t dir_inode, int depth);

// Gets block location (handles direct and indirect blocks)
int inode_get_block_location(struct inode *inode, uint32_t block_num, struct location *loc);

// Sets block location (creates indirect blocks as needed)
int inode_set_block_location(struct inode *inode, uint32_t block_num, struct location *loc);

#endif