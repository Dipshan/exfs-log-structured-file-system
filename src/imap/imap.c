// Filename: imap.c
// Manages the in-memory cache for Inode locations and handles
// flushing the mapping table to persistent segment storage.

#include "imap.h"           // For imap structs and constants
#include "../fs/fs.h"       // For fs_read, fs_append
#include "../utils/utils.h" // For safe_read

// Maximum inodes supported (512 to match one 4KB chunk)
#define MAX_SYSTEM_INODES IMAP_CHUNK_SIZE

// In-memory cache: inode number -> physical location
static struct location imap_cache[MAX_SYSTEM_INODES];

// Highest inode number currently in use
static uint32_t max_inode = 0;

// Where the latest imap chunk lives in the log
static struct location current_imap_location;

// Set to 1 if cache has unsaved changes
static int imap_dirty = 0;

// Loads imap from checkpoint if exists, otherwise initializes empty cache
void imap_init(void)
{
    memset(imap_cache, 0xFF, sizeof(imap_cache)); // 0xFFFFFFFF means invalid/empty location
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

            // Load chunk into cache
            for (int i = 0; i < IMAP_CHUNK_SIZE && (chunk.start_inode + i) < MAX_SYSTEM_INODES; i++)
            {
                imap_cache[chunk.start_inode + i] = chunk.mappings[i];
                if (chunk.start_inode + i > max_inode)
                    max_inode = chunk.start_inode + i;
            }
        }
    }
}

// Writes entire imap cache to a 4KB chunk and appends to log
void imap_flush(void)
{
    struct imap_chunk chunk;
    chunk.start_inode = 0;

    // Copy valid mappings
    for (int i = 0; i < IMAP_CHUNK_SIZE && i <= max_inode; i++)
        chunk.mappings[i] = imap_cache[i];

    // Pad remaining with invalid markers
    for (int i = max_inode + 1; i < IMAP_CHUNK_SIZE; i++)
    {
        chunk.mappings[i].segment_id = 0xFFFFFFFF;
        chunk.mappings[i].offset = 0;
    }

    fs_append(&chunk, sizeof(chunk), &current_imap_location);
    imap_dirty = 0;
}

// Returns current segment/offset where imap is stored
struct location imap_get_current_location(void)
{
    return current_imap_location;
}

// Updates cache with new location for an inode
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

    // Flush every 10 updates for crash safety
    static int update_counter = 0;
    if (++update_counter >= 10)
    {
        imap_flush();
        update_counter = 0;
    }
}

// Returns physical location of an inode, or invalid if not found
struct location imap_lookup(uint32_t inode_num)
{
    struct location empty = {0xFFFFFFFF, 0};

    if (inode_num > max_inode || inode_num >= MAX_SYSTEM_INODES)
        return empty;

    if (imap_cache[inode_num].segment_id != 0xFFFFFFFF)
        return imap_cache[inode_num];

    return empty;
}