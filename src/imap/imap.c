#include "imap.h"
#include "../fs/fs.h"
#include "../utils/utils.h"

// In-memory cache of imap
static struct location imap_cache[10000]; // Support up to 10000 inodes
static uint32_t max_inode = 0;
static struct location current_imap_location; // Where the latest imap chunk lives
static int imap_dirty = 0;

// Initialize imap (load from checkpoint or create empty)
void imap_init(void)
{
    memset(imap_cache, 0xFF, sizeof(imap_cache)); // 0xFFFFFFFF means invalid
    max_inode = 0;

    // Try to load imap location from checkpoint
    struct checkpoint chk;
    FILE *f = fopen("checkpoint.bin", "rb");
    if (f)
    {
        safe_read(f, &chk, sizeof(chk));
        fclose(f);

        if (chk.imap_location.segment_id != 0xFFFFFFFF)
        {
            current_imap_location = chk.imap_location;

            // Load imap chunk from segment
            struct imap_chunk chunk;
            fs_read(&current_imap_location, &chunk, sizeof(chunk));

            // Populate cache
            for (int i = 0; i < IMAP_CHUNK_SIZE && (chunk.start_inode + i) < 10000; i++)
            {
                imap_cache[chunk.start_inode + i] = chunk.mappings[i];
                if (chunk.start_inode + i > max_inode)
                    max_inode = chunk.start_inode + i;
            }
        }
    }
}

// Write the entire imap to segments
void imap_flush(void)
{
    // Write imap chunks for all inodes
    // For simplicity, write a single chunk covering all inodes
    struct imap_chunk chunk;
    chunk.start_inode = 0;

    for (int i = 0; i < IMAP_CHUNK_SIZE && i <= max_inode; i++)
    {
        chunk.mappings[i] = imap_cache[i];
    }

    // Pad remaining with invalid
    for (int i = max_inode + 1; i < IMAP_CHUNK_SIZE; i++)
    {
        chunk.mappings[i].segment_id = 0xFFFFFFFF;
        chunk.mappings[i].offset = 0;
    }

    // Write to log
    fs_append(&chunk, sizeof(chunk), &current_imap_location);
    imap_dirty = 0;
}

// Get current imap location for checkpoint
struct location imap_get_current_location(void)
{
    return current_imap_location;
}

// Update the map with new location for an inode
void imap_update(uint32_t inode_num, struct location *loc)
{
    if (inode_num >= 10000)
    {
        printf("ERROR: Too many inodes (max 10000)\n");
        return;
    }

    imap_cache[inode_num] = *loc;
    imap_dirty = 1;

    if (inode_num > max_inode)
    {
        max_inode = inode_num;
    }

    // Flush every 10 updates to keep things persistent
    static int update_counter = 0;
    if (++update_counter >= 10)
    {
        imap_flush();
        update_counter = 0;
    }
}

// Find where an inode lives, returns location
struct location imap_lookup(uint32_t inode_num)
{
    struct location empty = {0xFFFFFFFF, 0};

    if (inode_num > max_inode)
    {
        return empty;
    }

    // If we have it in cache, use it
    if (imap_cache[inode_num].segment_id != 0xFFFFFFFF)
    {
        return imap_cache[inode_num];
    }

    return empty;
}