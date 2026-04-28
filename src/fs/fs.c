// Filename: fs.c
// Core file system implementation.
// Handles segment I/O operations, checkpointing, file/directory addition, extraction, and garbage collection.

#include "fs.h"             // For checkpoint struct and FS API functions
#include "../inode/inode.h" // For inode operations and directory handling
#include "../imap/imap.h"   // For imap lookup, update, and flush
#include "../utils/utils.h" // For safe I/O and path resolution helpers

#include <unistd.h>   // For mkdir(), access(), fseek(), remove()
#include <sys/stat.h> // For file permissions (0755) in mkdir()
#include <dirent.h>   // For directory reading (opendir, readdir) in cleaner

// Checkpoint data synced from checkpoint.bin
static struct checkpoint checkpoint;

// File pointer to currently open segment
static FILE *current_file = NULL;

// Mount flag - Set to 1 after fs_init() succeeds - prevents re-initialization crashes
static int is_mounted = 0;

// Builds path for a given segment ID
static void get_segment_path(uint32_t id, char *path)
{
    sprintf(path, "segments/segment%u.bin", id);
}

// Opens the current active segment and seeks to the active write offset
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

// Generates a new 1MB segment file on the host disk
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

    // Make file exactly 1MB
    fseek(f, SEGMENT_SIZE - 1, SEEK_SET);
    fputc(0, f);
    fclose(f);
}

// Mounts FS by loading the checkpoint region,
// or initializes a completely fresh FS and root directory if none exists.
void fs_init(void)
{
    if (is_mounted)
        return;

    // Create segments directory - 0755 means the directory has read, write, and execute permissions for the owner, and read+execute for everyone else (group and others).
    mkdir("segments", 0755);
    FILE *f = fopen("checkpoint.bin", "rb");

    if (f)
    {
        // Load existing FS
        safe_read(f, &checkpoint, sizeof(checkpoint));
        fclose(f);

        // Initialize dynamic imap from the stored checkpoint location
        imap_init();
        open_current_segment();
        fprintf(stderr, "FS loaded. Writing to segment %d at offset %d\n",
                checkpoint.active_segment, checkpoint.active_offset);
    }
    else
    {
        // Create new FS
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

        // Initialize all pointers to invalid or empty
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

// Saves checkpoint to disk
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

// Loads checkpoint from disk
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

// Append Protocol
// Appends raw data to the active offset,
// Auto-creates new segment if full
void fs_append(void *data, size_t size, struct location *loc)
{
    // Check if incoming data fits within the active segment
    if (checkpoint.active_offset + (int)size > SEGMENT_SIZE)
    {
        // Create new segment
        checkpoint.active_segment++;
        checkpoint.active_offset = 0;

        create_segment(checkpoint.active_segment);
        open_current_segment();
        fs_write_checkpoint();

        printf("Created new segment %d\n", checkpoint.active_segment);
    }

    // Record write location
    loc->segment_id = checkpoint.active_segment;
    loc->offset = checkpoint.active_offset;

    // Write data
    fseek(current_file, checkpoint.active_offset, SEEK_SET);
    safe_write(current_file, data, size);

    // Advance write head
    checkpoint.active_offset += size;

    // Save checkpoint every 5 writes
    static int counter = 0;
    if (++counter >= 5)
    {
        fs_write_checkpoint();
        counter = 0;
    }
}

// Reads data from specific log location
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

// Adds file from host to FS
void fs_add(const char *path, const char *source)
{
    // Open host file
    FILE *host_file = fopen(source, "rb");
    if (!host_file)
    {
        printf("ERROR: Cannot open host file '%s'\n", source);
        return;
    }

    // Get file size
    fseek(host_file, 0, SEEK_END);
    uint32_t file_size = ftell(host_file);
    fseek(host_file, 0, SEEK_SET);

    // Split path into parent and name
    char host_parent[1024], file_name[256];
    split_path(source, host_parent, file_name);

    // Create parent directories if needed
    if (strcmp(path, "/") != 0) {
    char dummy[1024];
    snprintf(dummy, sizeof(dummy), "%s/dummy", path);
    create_parent_dirs(dummy);
    }

    // Get parent directory inode
    uint32_t parent_inode = (strcmp(path, "/") == 0) ? 0 : find_inode(path);
    if (parent_inode == 0 && strcmp(path, "/") != 0)
    {
        printf("ERROR: Parent directory not found\n");
        fclose(host_file);
        return;
    }

    // Create new file inode
    uint32_t file_inode;
    inode_create(&file_inode, TYPE_FILE);
    dir_add_entry(parent_inode, file_name, file_inode);

    // Write all data blocks
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

    // Update inode size
    inode.size = file_size;
    inode_write(file_inode, &inode);

    printf("Added '%s' (%u bytes, %u blocks)\n", path, file_size, total_blocks);

    // Persist changes
    imap_flush();
    checkpoint.imap_location = imap_get_current_location();
    fs_write_checkpoint();
}

// Extracts file to stdout
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

// Removes a file or recursively destroys a directory
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

    // Remove entry from parent
    dir_remove_entry(parent_inode, name);
    printf("Removed '%s'\n", path);

    // Persist changes
    imap_flush();
    checkpoint.imap_location = imap_get_current_location();
    fs_write_checkpoint();
}

// Lists entire file system tree
void fs_list(void)
{
    printf("/ (root)\n");
    dir_list_recursive(0, 1);
}

// LFS Garbage Collector
// Scans the entire file system for "live" data blocks
// Migrates live data to the active segment
// Then permanently deletes older segments containing "dead" data
// For optimizing and reclaiming host disk space
void fs_cleaner(void)
{
    printf("\n----- CLEANER -----\n");
    printf("Active segment: %d, offset: %d\n", checkpoint.active_segment, checkpoint.active_offset);

    // Remember starting segment
    uint32_t starting_segment = checkpoint.active_segment;

    // Step 1: Collect all live inodes
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

    // Dynamically allocate memory for blocks
    struct location *live_blocks = malloc(100000 * sizeof(struct location));
    struct location *new_block_locations = malloc(100000 * sizeof(struct location));
    if (!live_blocks || !new_block_locations)
    {
        printf("ERROR: Memory allocation failed\n");
        exit(1);
    }

    // Step 2: Iterate through every live inode and extract valid data block pointers
    uint32_t block_count = 0;
    for (uint32_t i = 0; i < live_count; i++)
    {
        struct inode inode;
        fs_read(&live_inodes[i], &inode, sizeof(inode));

        // Force directory entries to yield 1 block else they compute as size 0
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

    // Step 3: Copy live inodes to new location
    struct location new_inode_locations[10000];
    for (uint32_t i = 0; i < live_count; i++)
    {
        struct inode inode;
        fs_read(&live_inodes[i], &inode, sizeof(inode));
        fs_append(&inode, sizeof(inode), &new_inode_locations[i]);
    }

    // Step 4: Copy live data blocks to new location
    for (uint32_t i = 0; i < block_count; i++)
    {
        char buffer[BLOCK_SIZE];
        fs_read(&live_blocks[i], buffer, BLOCK_SIZE);
        fs_append(buffer, BLOCK_SIZE, &new_block_locations[i]);
    }

    // Step 5: Update inodes with new block pointers
    uint32_t block_idx = 0;
    for (uint32_t i = 0; i < live_count; i++)
    {
        struct inode inode;
        fs_read(&live_inodes[i], &inode, sizeof(inode));

        uint32_t num_blocks = (inode.type == TYPE_DIRECTORY) ? 1 : (inode.size + BLOCK_SIZE - 1) / BLOCK_SIZE;

        struct inode new_inode;
        memcpy(&new_inode, &inode, sizeof(inode));

        // Reset block pointers
        for (int j = 0; j < 10; j++)
            new_inode.direct[j].segment_id = 0xFFFFFFFF;
        new_inode.single_indirect.segment_id = 0xFFFFFFFF;
        new_inode.double_indirect.segment_id = 0xFFFFFFFF;
        new_inode.triple_indirect.segment_id = 0xFFFFFFFF;

        // Set new block pointers
        for (uint32_t b = 0; b < num_blocks; b++)
            inode_set_block_location(&new_inode, b, &new_block_locations[block_idx++]);

        // Commit updated inode using its correct ID
        inode_write(live_inode_nums[i], &new_inode);
    }

    // Step 6: Delete old segment files (data written before the cleaner started)
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

            // Only delete segments older than starting_segment (preserves newly written data)
            if (seg_id < starting_segment)
            {
                char path[100];
                sprintf(path, "segments/%s", entry->d_name);
                remove(path);
            }
        }
        closedir(dir);
    }

    // Free allocated memory
    free(live_blocks);
    free(new_block_locations);

    // Step 7: Flush imap and update checkpoint
    imap_flush();
    checkpoint.imap_location = imap_get_current_location();
    fs_write_checkpoint();

    printf("----- CLEANER COMPLETE -----\n");
}

// Debug - prints inode metadata, direct/indirect block pointers, directory entries, and log location
void fs_debug(const char *path)
{
    // Find inode number by walking the path
    uint32_t inode_num = find_inode(path);
    if (inode_num == 0)
    {
        printf("ERROR: '%s' not found\n", path);
        return;
    }

    // Read inode from disk using imap lookup
    struct inode inode;
    inode_read(inode_num, &inode);

    // Print basic inode info
    printf("\n--- INODE DEBUG ---\n");
    printf("Path: %s\nInode: %u\nType: %s\nSize: %u bytes\n",
           path, inode_num,
           inode.type == TYPE_FILE ? "FILE" : "DIRECTORY",
           inode.size);

    // Print all 10 direct block pointers
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

    // Print indirect block status
    printf("\nIndirect:\n  Single: %s\n  Double: %s\n  Triple: %s\n",
           inode.single_indirect.segment_id != 0xFFFFFFFF ? "yes" : "no",
           inode.double_indirect.segment_id != 0xFFFFFFFF ? "yes" : "no",
           inode.triple_indirect.segment_id != 0xFFFFFFFF ? "yes" : "no");

    // If directory, list all entries from its first data block
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

    // Show where this inode lives in the log
    struct location loc = imap_lookup(inode_num);
    printf("\nLog location: seg %d, off %d\n\n", loc.segment_id, loc.offset);
}