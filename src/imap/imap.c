/**
 * ExFS-Log: Log-Structured File System
 * * imap.c
 * Manages the high-speed RAM cache for Inode locations and handles 
 * flushing the mapping table down to persistent storage.
 */

#include "imap.h"
#include "../fs/fs.h"
#include "../utils/utils.h"

// System bounded to 512 to prevent cache-to-disk persistence loss
#define MAX_SYSTEM_INODES IMAP_CHUNK_SIZE

static struct location imap_cache[MAX_SYSTEM_INODES];
static uint32_t max_inode = 0;
static struct location current_imap_location; 
static int imap_dirty = 0;

/**
 * Boots the Imap. Loads the persistent chunk from the disk if a checkpoint 
 * exists, otherwise initializes a blank cache for a fresh mount.
 */
void imap_init(void)
{
    memset(imap_cache, 0xFF, sizeof(imap_cache)); // 0xFFFFFFFF represents an invalid/empty state
    max_inode = 0;

    struct checkpoint chk;
    FILE *f = fopen("checkpoint.bin", "rb");
    if (f)
    {
        safe_read(f, &chk, sizeof(chk));
        fclose(f);

        if (chk.imap_location.segment_id != 0xFFFFFFFF)
        {
            current_imap_location = chk.imap_location;

            struct imap_chunk chunk;
            fs_read(&current_imap_location, &chunk, sizeof(chunk));

            // Safely populate the RAM cache from the disk chunk
            for (int i = 0; i < IMAP_CHUNK_SIZE && (chunk.start_inode + i) < MAX_SYSTEM_INODES; i++)
            {
                imap_cache[chunk.start_inode + i] = chunk.mappings[i];
                if (chunk.start_inode + i > max_inode)
                    max_inode = chunk.start_inode + i;
            }
        }
    }
}

/**
 * Commits the volatile RAM cache into a 4KB chunk and appends it to the log.
 */
void imap_flush(void)
{
    struct imap_chunk chunk;
    chunk.start_inode = 0;

    // Pack valid inodes
    for (int i = 0; i < IMAP_CHUNK_SIZE && i <= max_inode; i++)
        chunk.mappings[i] = imap_cache[i];

    // Pad remaining slots with invalid markers
    for (int i = max_inode + 1; i < IMAP_CHUNK_SIZE; i++)
    {
        chunk.mappings[i].segment_id = 0xFFFFFFFF;
        chunk.mappings[i].offset = 0;
    }

    fs_append(&chunk, sizeof(chunk), &current_imap_location);
    imap_dirty = 0;
}

struct location imap_get_current_location(void)
{
    return current_imap_location;
}

void imap_update(uint32_t inode_num, struct location *loc)
{
    if (inode_num >= MAX_SYSTEM_INODES)
    {
        printf("ERROR: System inode capacity reached (max %d)\n", MAX_SYSTEM_INODES);
        return;
    }

    imap_cache[inode_num] = *loc;
    imap_dirty = 1;

    if (inode_num > max_inode)
        max_inode = inode_num;

    // Persist every 10 updates to balance I/O overhead with crash safety
    static int update_counter = 0;
    if (++update_counter >= 10)
    {
        imap_flush();
        update_counter = 0;
    }
}

struct location imap_lookup(uint32_t inode_num)
{
    struct location empty = {0xFFFFFFFF, 0};

    if (inode_num > max_inode || inode_num >= MAX_SYSTEM_INODES)
        return empty;

    if (imap_cache[inode_num].segment_id != 0xFFFFFFFF)
        return imap_cache[inode_num];

    return empty;
}