#ifndef IMAP_H
#define IMAP_H

#include "../common.h"

// Imap chunk - stored in segments
// Each chunk contains a range of inode mappings
#define IMAP_CHUNK_SIZE 512 // Number of mappings per chunk (512 * 8 = 4096 bytes)

struct imap_chunk
{
    uint32_t start_inode;                      // First inode number in this chunk
    struct location mappings[IMAP_CHUNK_SIZE]; // Locations for each inode
};

// Initialize imap (load from checkpoint or create empty)
void imap_init(void);

// Record where an inode lives in the log
void imap_update(uint32_t inode_num, struct location *loc);

// Find where an inode lives
struct location imap_lookup(uint32_t inode_num);

// Write the entire imap to segments (called during checkpoint)
void imap_flush(void);

// Get the current imap location for checkpoint
struct location imap_get_current_location(void);

#endif