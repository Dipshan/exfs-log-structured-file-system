// Filename: imap.h
// Defines the Inode Map (Imap) structures.
// The Imap decouples Inode IDs from their physical locations,
// allowing the LFS to freely relocate inodes to the tail of the log during writes and garbage collection.

#ifndef IMAP_H
#define IMAP_H

#include "../common.h" // For BLOCK_SIZE, location struct

// Imap chunk - strictly sized to fit inside a single 4KB block
// 512 mappings * 8 bytes (struct location) = exactly 4096 bytes
#define IMAP_CHUNK_SIZE 512

struct imap_chunk
{
    uint32_t start_inode;                      // First inode number in this chunk
    struct location mappings[IMAP_CHUNK_SIZE]; // Physical locations for each inode
};

// Loads imap from checkpoint during mount
void imap_init(void);

// Records new location for an inode in the cache
void imap_update(uint32_t inode_num, struct location *loc);

// Returns physical location of an inode, or invalid (0xFFFFFFFF) if not found
struct location imap_lookup(uint32_t inode_num);

// Writes entire imap cache to a segment (persistence)
void imap_flush(void);

// Returns current segment/offset where imap is stored
struct location imap_get_current_location(void);

#endif