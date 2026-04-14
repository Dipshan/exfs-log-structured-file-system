#ifndef FS_H
#define FS_H

#include "../common.h"

// Checkpoint Region - 4096 bytes total
struct checkpoint
{
    uint32_t active_segment;       // Current segment being written to
    uint32_t active_offset;        // Current write position in that segment
    struct location imap_location; // Where the latest imap chunk lives
    uint32_t next_inode_num;       // Next free inode number to assign
    uint32_t reserved[1019];       // Padding to 4096 bytes
};

// File system operations (called from main.c)
void fs_init(void);
void fs_add(const char *path, const char *source);
void fs_remove(const char *path);
void fs_extract(const char *path);
void fs_list(void);
void fs_cleaner(void);
void fs_debug(const char *path);

// Internal FS functions
void fs_append(void *data, size_t size, struct location *loc);
void fs_read(struct location *loc, void *buffer, size_t size);
void fs_create_new_segment(void);
void fs_write_checkpoint(void);
void fs_read_checkpoint(void);

// Get current checkpoint info
void fs_get_checkpoint(struct checkpoint *chk);

#endif