// Filename: fs.h
// Defines the checkpoint region and declares all file system operations.
// Serves as the public API for mounting, adding, extracting, removing, listing, cleaning, and debugging.

#ifndef FS_H
#define FS_H

#include "../common.h" // For BLOCK_SIZE, SEGMENT_SIZE, location struct

// Checkpoint Region - 4096 bytes total
struct checkpoint
{
    uint32_t active_segment;       // Current segment being written to
    uint32_t active_offset;        // Current write position in that segment
    struct location imap_location; // Where the latest imap chunk lives
    uint32_t next_inode_num;       // Next free inode number to assign
    uint32_t reserved[1019];       // Padding to 4096 bytes
};

// Mounts existing FS or creates a new one
void fs_init(void);

// Copies a file from host machine into the FS at specified path
void fs_add(const char *path, const char *source);

// Deletes a file or recursively removes a directory
void fs_remove(const char *path);

// Writes file contents to stdout
void fs_extract(const char *path);

// Prints entire directory tree with indentation
void fs_list(void);

// Compacts live data and reclaims space from dead segments
void fs_cleaner(void);

// Shows inode metadata, block pointers, and directory entries
void fs_debug(const char *path);

// Internal FS functions
void fs_append(void *data, size_t size, struct location *loc); // Appends data to log
void fs_read(struct location *loc, void *buffer, size_t size); // Reads from log location
void fs_write_checkpoint(void);                                // Persists checkpoint to disk
void fs_read_checkpoint(void);                                 // Loads checkpoint from disk

#endif