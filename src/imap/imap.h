/**
 * ExFS-Log: Log-Structured File System
 * * imap.h
 * Defines the Inode Map (Imap) structures. The Imap decouples Inode IDs 
 * from their physical locations, allowing the LFS to freely relocate 
 * inodes to the tail of the log during writes and garbage collection.
 */

#ifndef IMAP_H
#define IMAP_H

#include "../common.h"

// Imap chunk - strictly sized to fit inside a single 4KB block
// 512 mappings * 8 bytes (struct location) = exactly 4096 bytes
#define IMAP_CHUNK_SIZE 512 

struct imap_chunk
{
    uint32_t start_inode;                      // First inode number in this chunk
    struct location mappings[IMAP_CHUNK_SIZE]; // Physical locations for each inode
};

void imap_init(void);
void imap_update(uint32_t inode_num, struct location *loc);
struct location imap_lookup(uint32_t inode_num);
void imap_flush(void);
struct location imap_get_current_location(void);

#endif