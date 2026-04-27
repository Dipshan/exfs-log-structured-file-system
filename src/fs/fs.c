/**
 * ExFS-Log: Log-Structured File System
 * * fs.c
 * Core file system implementation. Handles segment I/O operations,
 * checkpointing, file/directory addition, extraction, and garbage collection.
 * As an LFS, all data (metadata and file data) is strictly appended to the 
 * end of the active segment file to maximize write performance.
 */

#include "fs.h"
#include "../inode/inode.h"
#include "../imap/imap.h"
#include "../utils/utils.h"

#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>

// --- System State Variables ---
static struct checkpoint checkpoint;
static FILE *current_file = NULL;
static int is_mounted = 0;

/**
 * Helper: Constructs the string path for a given segment ID.
 */
static void get_segment_path(uint32_t id, char *path)
{
    sprintf(path, "segments/segment%u.bin", id);
}

/**
 * Helper: Opens the current active segment and seeks to the active write offset.
 */
static void open_current_segment(void)
{
    char path[100];
    get_segment_path(checkpoint.active_segment, path);

    if (current_file)
        fclose(current_file);

    current_file = fopen(path, "rb+");
    if (!current_file)
    {
        printf("ERROR: Cannot open segment %d\n", checkpoint.active_segment);
        exit(1);
    }

    fseek(current_file, checkpoint.active_offset, SEEK_SET);
}

/**
 * Helper: Generates a new, zeroed 1MB segment file on the host disk.
 */
static void create_segment(uint32_t id)
{
    char path[100];
    get_segment_path(id, path);

    FILE *f = fopen(path, "wb");
    if (!f)
    {
        printf("ERROR: Cannot create segment %d\n", id);
        exit(1);
    }

    // Force the file to instantly allocate 1MB of space
    fseek(f, SEGMENT_SIZE - 1, SEEK_SET);
    fputc(0, f);
    fclose(f);
}

/**
 * Mounts the file system by loading the checkpoint region, or initializes 
 * a completely fresh file system and root directory if none exists.
 */
void fs_init(void)
{
    if (is_mounted)
        return;

    // Create segments directory (permissions: rwxr-xr-x)
    mkdir("segments", 0755);
    FILE *f = fopen("checkpoint.bin", "rb");

    if (f)
    {
        // Load existing file system state
        safe_read(f, &checkpoint, sizeof(checkpoint));
        fclose(f);

        // Initialize dynamic imap from the stored checkpoint location
        imap_init();

        open_current_segment();

        fprintf(stderr, "File system loaded. Writing to segment %d at offset %d\n",
                checkpoint.active_segment, checkpoint.active_offset);
    }
    else
    {
        // Initialize fresh file system
        printf("Creating new file system...\n");

        checkpoint.active_segment = 0;
        checkpoint.active_offset = 0;
        checkpoint.next_inode_num = 1; // Inode 0 is strictly reserved for root
        checkpoint.imap_location.segment_id = 0xFFFFFFFF;
        checkpoint.imap_location.offset = 0;

        create_segment(0);
        open_current_segment();

        imap_init();

        // Generate the root directory (Inode 0)
        struct inode root;
        memset(&root, 0, sizeof(root));
        root.type = TYPE_DIRECTORY;
        root.size = 0;

        // Initialize all pointers to invalid
        for (int i = 0; i < 10; i++)
            root.direct[i].segment_id = 0xFFFFFFFF;
        root.single_indirect.segment_id = 0xFFFFFFFF;
        root.double_indirect.segment_id = 0xFFFFFFFF;
        root.triple_indirect.segment_id = 0xFFFFFFFF;

        // Write root to the log and immediately update the imap
        struct location root_loc;
        fs_append(&root, sizeof(root), &root_loc);
        imap_update(0, &root_loc);

        // Flush imap and save the initialization checkpoint
        imap_flush();
        checkpoint.imap_location = imap_get_current_location();
        fs_write_checkpoint();

        printf("File system created.\n");
    }

    is_mounted = 1;
}

/**
 * Commits the current system state (active segment, active offset, imap location) 
 * to the permanent checkpoint file.
 */
void fs_write_checkpoint(void)
{
    FILE *f = fopen("checkpoint.bin", "wb");
    if (!f)
    {
        printf("ERROR: Cannot write checkpoint\n");
        return;
    }

    safe_write(f, &checkpoint, sizeof(checkpoint));
    fclose(f);
}

void fs_read_checkpoint(void)
{
    FILE *f = fopen("checkpoint.bin", "rb");
    if (!f)
    {
        printf("ERROR: Cannot read checkpoint\n");
        return;
    }

    safe_read(f, &checkpoint, sizeof(checkpoint));
    fclose(f);
}

/**
 * Core LFS Append Protocol. 
 * Writes raw data to the active offset. Automatically spans to a newly generated 
 * segment file if the 1MB limit is breached.
 */
void fs_append(void *data, size_t size, struct location *loc)
{
    // Check if incoming data fits within the active segment limit
    if (checkpoint.active_offset + (int)size > SEGMENT_SIZE)
    {
        // FIX: Removed the standalone fclose() to prevent Linux double-free crashes.
        // open_current_segment() automatically and safely closes the old file.
        checkpoint.active_segment++;
        checkpoint.active_offset = 0;

        create_segment(checkpoint.active_segment);
        open_current_segment();
        fs_write_checkpoint();

        printf("Created new segment %d\n", checkpoint.active_segment);
    }

    loc->segment_id = checkpoint.active_segment;
    loc->offset = checkpoint.active_offset;

    fseek(current_file, checkpoint.active_offset, SEEK_SET);
    safe_write(current_file, data, size);

    fflush(current_file);

    checkpoint.active_offset += size;

    static int counter = 0;
    if (++counter >= 5)
    {
        fs_write_checkpoint();
        counter = 0;
    }
}

/**
 * Retrieves raw data from a specific physical location in the log.
 */
void fs_read(struct location *loc, void *buffer, size_t size)
{
    if (loc->segment_id == 0xFFFFFFFF)
    {
        printf("ERROR: Invalid location\n");
        return;
    }

    char path[100];
    get_segment_path(loc->segment_id, path);

    FILE *f = fopen(path, "rb");
    if (!f)
    {
        printf("ERROR: Cannot read segment %d\n", loc->segment_id);
        return;
    }

    fseek(f, loc->offset, SEEK_SET);
    safe_read(f, buffer, size);
    fclose(f);
}

/**
 * Ingests a host file, fragments it into 4KB blocks, and appends the blocks,
 * updating the parent directory structures accordingly.
 */
void fs_add(const char *target_dir, const char *source_file)
{
    FILE *host_file = fopen(source_file, "rb");
    if (!host_file)
    {
        printf("ERROR: Cannot open host file '%s'\n", source_file);
        return;
    }

    fseek(host_file, 0, SEEK_END);
    uint32_t file_size = (uint32_t)ftell(host_file);
    fseek(host_file, 0, SEEK_SET);

    // FIX: Extract the actual file name from the host machine's source path
    char host_parent[1024], file_name[256];
    split_path(source_file, host_parent, file_name);

    // FIX: Force the creation of the full target directory structure
    if (strcmp(target_dir, "/") != 0)
    {
        char dummy_path[1024];
        snprintf(dummy_path, sizeof(dummy_path), "%s/dummy", target_dir);
        create_parent_dirs(dummy_path);
    }

    uint32_t parent_inode = (strcmp(target_dir, "/") == 0) ? 0 : find_inode(target_dir);
    if (parent_inode == 0 && strcmp(target_dir, "/") != 0)
    {
        printf("ERROR: Target directory not found\n");
        fclose(host_file);
        return;
    }

    uint32_t file_inode;
    inode_create(&file_inode, TYPE_FILE);
    dir_add_entry(parent_inode, file_name, file_inode);

    uint32_t total_blocks = (file_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    char buffer[BLOCK_SIZE];

    struct inode inode;
    inode_read(file_inode, &inode);

    for (uint32_t block_num = 0; block_num < total_blocks; block_num++)
    {
        int bytes_read = fread(buffer, 1, BLOCK_SIZE, host_file);
        if (bytes_read == 0)
            break;

        if (bytes_read < BLOCK_SIZE)
            memset(buffer + bytes_read, 0, BLOCK_SIZE - bytes_read);

        struct location data_loc;
        fs_append(buffer, BLOCK_SIZE, &data_loc);

        if (!inode_set_block_location(&inode, block_num, &data_loc))
        {
            printf("ERROR: Failed to set block %d\n", block_num);
            break;
        }
    }

    fclose(host_file);

    inode.size = file_size;
    inode_write(file_inode, &inode);

    // Ensure clean printout of the new path
    char full_fs_path[1024];
    if (strcmp(target_dir, "/") == 0)
        snprintf(full_fs_path, sizeof(full_fs_path), "/%s", file_name);
    else
        snprintf(full_fs_path, sizeof(full_fs_path), "%s/%s", target_dir, file_name);

    printf("Added '%s' (%u bytes, %u blocks)\n", full_fs_path, file_size, total_blocks);

    imap_flush();
    checkpoint.imap_location = imap_get_current_location();
    fs_write_checkpoint();
}

/**
 * Reads a file's blocks sequentially and pipes the raw data to stdout.
 */
void fs_extract(const char *path)
{
    uint32_t inode_num = find_inode(path);
    if (inode_num == 0)
    {
        printf("ERROR: File '%s' not found\n", path);
        return;
    }

    struct inode inode;
    inode_read(inode_num, &inode);

    if (inode.type != TYPE_FILE)
    {
        printf("ERROR: '%s' is a directory\n", path);
        return;
    }

    uint32_t bytes_remaining = inode.size, block_num = 0;
    char buffer[BLOCK_SIZE];

    // Read and pipe data via stdout until EOF
    while (bytes_remaining > 0)
    {
        struct location data_loc;
        if (!inode_get_block_location(&inode, block_num, &data_loc) ||
            data_loc.segment_id == 0xFFFFFFFF)
            break;

        fs_read(&data_loc, buffer, BLOCK_SIZE);

        uint32_t to_write = (bytes_remaining < BLOCK_SIZE) ? bytes_remaining : BLOCK_SIZE;
        fwrite(buffer, 1, to_write, stdout);

        bytes_remaining -= to_write;
        block_num++;
    }
}

/**
 * Removes a file or recursively destroys a directory. 
 */
void fs_remove(const char *path)
{
    uint32_t inode_num = find_inode(path);
    if (inode_num == 0)
    {
        printf("ERROR: '%s' not found\n", path);
        return;
    }

    char parent_path[1024], name[256];
    split_path(path, parent_path, name);

    uint32_t parent_inode = (strcmp(parent_path, "/") == 0) ? 0 : find_inode(parent_path);
    if (parent_inode == 0 && strcmp(parent_path, "/") != 0)
    {
        printf("ERROR: Parent directory not found\n");
        return;
    }

    struct inode inode;
    inode_read(inode_num, &inode);

    // Recursively handle sub-directories
    if (inode.type == TYPE_DIRECTORY)
    {
        printf("Removing directory '%s' and all contents\n", path);

        if (inode.direct[0].segment_id != 0xFFFFFFFF)
        {
            struct directory_entry entries[DIR_ENTRIES_PER_BLOCK];
            fs_read(&inode.direct[0], entries, BLOCK_SIZE);

            for (int i = 0; i < DIR_ENTRIES_PER_BLOCK; i++)
            {
                if (entries[i].inode_num != 0)
                {
                    char child_path[1024];
                    if (strcmp(path, "/") == 0)
                        sprintf(child_path, "/%s", entries[i].name);
                    else
                        sprintf(child_path, "%s/%s", path, entries[i].name);
                    fs_remove(child_path);
                }
            }
        }
    }

    dir_remove_entry(parent_inode, name);
    printf("Removed '%s'\n", path);

    imap_flush();
    checkpoint.imap_location = imap_get_current_location();
    fs_write_checkpoint();
}

void fs_list(void)
{
    printf("/ (root)\n");
    dir_list_recursive(0, 1);
}

/**
 * LFS Garbage Collector.
 * Scans the entire file system for "live" data blocks. Migrates live data to 
 * the active segment, then permanently deletes older segments containing "dead" data
 * to optimize and reclaim host disk space.
 */
void fs_cleaner(void)
{
    printf("\n----- CLEANER -----\n");
    printf("Current active segment: %d\n", checkpoint.active_segment);
    printf("Current write offset: %d\n\n", checkpoint.active_offset);

    // Memorize the segment we started on so we don't delete freshly migrated data
    uint32_t starting_segment = checkpoint.active_segment;

    // Step 1: Sweep the imap for all active, live inodes
    struct location live_inodes[10000];
    uint32_t live_inode_nums[10000]; 
    uint32_t live_count = 0;

    for (uint32_t i = 0; i < 10000; i++)
    {
        struct location loc = imap_lookup(i);
        if (loc.segment_id != 0xFFFFFFFF)
        {
            live_inodes[live_count] = loc;
            live_inode_nums[live_count] = i; 
            live_count++;
        }
    }
    printf("Live inodes: %u\n", live_count);

    // OPTIMIZATION: Dynamically allocate massive tracking arrays on the Heap 
    // to prevent stack overflows on strict Linux operating systems.
    struct location *live_blocks = malloc(100000 * sizeof(struct location));
    struct location *new_block_locations = malloc(100000 * sizeof(struct location));
    if (!live_blocks || !new_block_locations) {
        printf("ERROR: Critical memory allocation failure during cleanup phase.\n");
        exit(1);
    }

    // Step 2: Iterate through every live inode and extract valid data block pointers
    uint32_t block_count = 0;
    for (uint32_t i = 0; i < live_count; i++)
    {
        struct inode inode;
        fs_read(&live_inodes[i], &inode, sizeof(inode));
        
        // Force directory entries to yield 1 block, otherwise they compute as size 0
        uint32_t num_blocks = (inode.type == TYPE_DIRECTORY) ? 1 : (inode.size + BLOCK_SIZE - 1) / BLOCK_SIZE;

        for (uint32_t b = 0; b < num_blocks; b++)
        {
            struct location block_loc;
            if (inode_get_block_location(&inode, b, &block_loc) &&
                block_loc.segment_id != 0xFFFFFFFF)
                live_blocks[block_count++] = block_loc;
        }
    }
    printf("Live blocks: %u\n", block_count);

    // Step 3: Migrate live inodes to the fresh tail of the log
    struct location new_inode_locations[10000];
    for (uint32_t i = 0; i < live_count; i++)
    {
        struct inode inode;
        fs_read(&live_inodes[i], &inode, sizeof(inode));
        fs_append(&inode, sizeof(inode), &new_inode_locations[i]);
    }

    // Step 4: Migrate live data blocks to the fresh tail of the log
    for (uint32_t i = 0; i < block_count; i++)
    {
        char buffer[BLOCK_SIZE];
        fs_read(&live_blocks[i], buffer, BLOCK_SIZE);
        fs_append(buffer, BLOCK_SIZE, &new_block_locations[i]);
    }

    // Step 5: Re-link newly migrated block pointers to their newly migrated inodes
    uint32_t block_idx = 0;
    for (uint32_t i = 0; i < live_count; i++)
    {
        struct inode inode;
        fs_read(&live_inodes[i], &inode, sizeof(inode));
        
        uint32_t num_blocks = (inode.type == TYPE_DIRECTORY) ? 1 : (inode.size + BLOCK_SIZE - 1) / BLOCK_SIZE;

        struct inode new_inode;
        memcpy(&new_inode, &inode, sizeof(inode));

        // Purge old block pointers
        for (int j = 0; j < 10; j++)
            new_inode.direct[j].segment_id = 0xFFFFFFFF;
        new_inode.single_indirect.segment_id = 0xFFFFFFFF;
        new_inode.double_indirect.segment_id = 0xFFFFFFFF;
        new_inode.triple_indirect.segment_id = 0xFFFFFFFF;

        // Assign new pointers
        for (uint32_t b = 0; b < num_blocks; b++)
            inode_set_block_location(&new_inode, b, &new_block_locations[block_idx++]);

        // Commit updated inode using its correct identity ID
        inode_write(live_inode_nums[i], &new_inode);
    }

    // Step 6: Prune old segment files (Delete data written before the cleaner started)
    DIR *dir = opendir("segments");
    if (dir)
    {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL)
        {
            if (entry->d_name[0] == '.')
                continue;

            uint32_t seg_id;
            sscanf(entry->d_name, "segment%u.bin", &seg_id);

            // Delete dead segments, preserve newly written active segments
            if (seg_id < starting_segment)
            {
                char path[100];
                sprintf(path, "segments/%s", entry->d_name);
                remove(path);
            }
        }
        closedir(dir);
    }

    // Free heap memory allocations
    free(live_blocks);
    free(new_block_locations);

    // Step 7: Flush new states to persistent checkpoint
    imap_flush();
    checkpoint.imap_location = imap_get_current_location();
    fs_write_checkpoint();

    printf("----- CLEANER COMPLETE -----\n");
}

void fs_debug(const char *path)
{
    uint32_t inode_num = find_inode(path);
    if (inode_num == 0)
    {
        printf("ERROR: '%s' not found\n", path);
        return;
    }

    struct inode inode;
    inode_read(inode_num, &inode);

    printf("\n--- INODE DEBUG ---\n");
    printf("Path: %s\nInode: %u\nType: %s\nSize: %u bytes\n",
           path, inode_num,
           inode.type == TYPE_FILE ? "FILE" : "DIRECTORY",
           inode.size);

    printf("\nDirect blocks:\n");
    int has_blocks = 0;
    for (int i = 0; i < 10; i++)
    {
        if (inode.direct[i].segment_id != 0xFFFFFFFF)
        {
            printf("  [%d] -> seg %d, off %d\n",
                   i, inode.direct[i].segment_id, inode.direct[i].offset);
            has_blocks = 1;
        }
    }
    if (!has_blocks)
        printf("  (none)\n");

    printf("\nIndirect:\n  Single: %s\n  Double: %s\n  Triple: %s\n",
           inode.single_indirect.segment_id != 0xFFFFFFFF ? "yes" : "no",
           inode.double_indirect.segment_id != 0xFFFFFFFF ? "yes" : "no",
           inode.triple_indirect.segment_id != 0xFFFFFFFF ? "yes" : "no");

    if (inode.type == TYPE_DIRECTORY && inode.direct[0].segment_id != 0xFFFFFFFF)
    {
        printf("\nDirectory contents:\n");
        struct directory_entry entries[DIR_ENTRIES_PER_BLOCK];
        fs_read(&inode.direct[0], entries, BLOCK_SIZE);

        for (int i = 0; i < DIR_ENTRIES_PER_BLOCK; i++)
        {
            if (entries[i].inode_num != 0)
                printf("  %s -> inode %d\n", entries[i].name, entries[i].inode_num);
        }
    }

    struct location loc = imap_lookup(inode_num);
    printf("\nLog location: seg %d, off %d\n\n", loc.segment_id, loc.offset);
}