#include "fs.h"
#include "../inode/inode.h"
#include "../imap/imap.h"
#include "../utils/utils.h"

#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>

// Current state of our file system
static struct checkpoint checkpoint;
static FILE *current_file = NULL;
static int is_mounted = 0;

// Make full path for a segment file
static void get_segment_path(uint32_t id, char *path)
{
    sprintf(path, "segments/segment%u.bin", id);
}

// Open current segment file at the right position for writing
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

// Create a new empty segment file of exactly 1MB
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

// Initialize the file system
void fs_init(void)
{
    if (is_mounted)
        return;

    // Create segments directory if needed
    mkdir("segments", 0755);
    FILE *f = fopen("checkpoint.bin", "rb");

    if (f)
    {
        // Load existing file system
        safe_read(f, &checkpoint, sizeof(checkpoint));
        fclose(f);

        // Initialize imap from checkpoint
        imap_init();

        open_current_segment();

        fprintf(stderr, "File system loaded. Writing to segment %d at offset %d\n",
                checkpoint.active_segment, checkpoint.active_offset);
    }
    else
    {
        // Create new file system
        printf("Creating new file system...\n");

        checkpoint.active_segment = 0;
        checkpoint.active_offset = 0;
        checkpoint.next_inode_num = 1; // 0 is reserved for root
        checkpoint.imap_location.segment_id = 0xFFFFFFFF;
        checkpoint.imap_location.offset = 0;

        create_segment(0);
        open_current_segment();

        // Initialize imap
        imap_init();

        // Create root directory (inode 0)
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

        // Write root to log and update imap
        struct location root_loc;
        fs_append(&root, sizeof(root), &root_loc);

        // Save where root is
        imap_update(0, &root_loc);

        // Flush imap to segments
        imap_flush();

        // Update checkpoint with imap location
        checkpoint.imap_location = imap_get_current_location();
        fs_write_checkpoint();

        printf("File system created.\n");
    }

    is_mounted = 1;
}

// Save checkpoint to checkpoint.bin
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

// Load checkpoint from checkpoint.bin
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

// Append data to the end of the log
void fs_append(void *data, size_t size, struct location *loc)
{
    // Check if data fits in current segment
    if (checkpoint.active_offset + (int)size > SEGMENT_SIZE)
    {
        // Current segment full - create new one
        if (current_file)
            fclose(current_file);

        checkpoint.active_segment++;
        checkpoint.active_offset = 0;

        create_segment(checkpoint.active_segment);
        open_current_segment();
        fs_write_checkpoint();

        printf("Created new segment %d\n", checkpoint.active_segment);
    }

    // Record where we wrote it
    loc->segment_id = checkpoint.active_segment;
    loc->offset = checkpoint.active_offset;

    // Write the data
    fseek(current_file, checkpoint.active_offset, SEEK_SET);
    safe_write(current_file, data, size);

    // Advance write head
    checkpoint.active_offset += size;

    // Save checkpoint every 5 writes (crash recovery)
    static int counter = 0;
    if (++counter >= 5)
    {
        fs_write_checkpoint();
        counter = 0;
    }
}

// Read data from specific location in log
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

// Add a file from host to our file system
void fs_add(const char *path, const char *source)
{
    // Open source file
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

    // Split path into parent directory and filename
    char parent_path[1024], file_name[256];
    split_path(path, parent_path, file_name);

    // Create parent directories if they don't exist
    if (strcmp(parent_path, "/") != 0 && !path_exists(parent_path))
        create_parent_dirs(path);

    // Get parent directory inode
    uint32_t parent_inode = (strcmp(parent_path, "/") == 0) ? 0 : find_inode(parent_path);
    if (parent_inode == 0 && strcmp(parent_path, "/") != 0)
    {
        printf("ERROR: Parent directory not found\n");
        fclose(host_file);
        return;
    }

    // Create new file inode
    uint32_t file_inode;
    inode_create(&file_inode, TYPE_FILE);

    // Add entry to parent directory
    dir_add_entry(parent_inode, file_name, file_inode);

    // Calculate total blocks needed
    uint32_t total_blocks = (file_size + BLOCK_SIZE - 1) / BLOCK_SIZE;

    // Read file and write data blocks
    char buffer[BLOCK_SIZE];
    struct inode inode;
    inode_read(file_inode, &inode);

    for (uint32_t block_num = 0; block_num < total_blocks; block_num++)
    {
        int bytes_read = fread(buffer, 1, BLOCK_SIZE, host_file);
        if (bytes_read == 0)
            break;

        // Pad last block with zeros
        if (bytes_read < BLOCK_SIZE)
            memset(buffer + bytes_read, 0, BLOCK_SIZE - bytes_read);

        // Write data block to log
        struct location data_loc;
        fs_append(buffer, BLOCK_SIZE, &data_loc);

        // Update inode block pointer (handles direct/indirect)
        if (!inode_set_block_location(&inode, block_num, &data_loc))
        {
            printf("ERROR: Failed to set block %d\n", block_num);
            break;
        }
    }

    fclose(host_file);

    // Update and save inode
    inode.size = file_size;
    inode_write(file_inode, &inode);

    printf("Added '%s' (%u bytes, %u blocks)\n", path, file_size, total_blocks);

    // Persist imap and checkpoint
    imap_flush();
    checkpoint.imap_location = imap_get_current_location();
    fs_write_checkpoint();
}

// Extract a file to stdout
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

    // Read and output each block (handles indirect blocks)
    uint32_t bytes_remaining = inode.size, block_num = 0;
    char buffer[BLOCK_SIZE];

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

// Remove a file or directory
void fs_remove(const char *path)
{
    uint32_t inode_num = find_inode(path);
    if (inode_num == 0)
    {
        printf("ERROR: '%s' not found\n", path);
        return;
    }

    // Split path to get parent
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

    // If it's a directory, remove all contents first
    if (inode.type == TYPE_DIRECTORY)
    {
        printf("Removing directory '%s' and all contents\n", path);

        // Read directory entries
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

    // Remove entry from parent directory
    dir_remove_entry(parent_inode, name);
    printf("Removed '%s'\n", path);

    // Flush the in-memory index and save the checkpoint before exiting
    imap_flush();
    checkpoint.imap_location = imap_get_current_location();
    fs_write_checkpoint();
}

// List entire file system tree
void fs_list(void)
{
    printf("/ (root)\n");
    dir_list_recursive(0, 1);
}

// Cleaner implementation - compacts live data, deletes old segments, updates checkpoint
void fs_cleaner(void)
{
    printf("\n----- CLEANER -----\n");
    printf("Current active segment: %d\n", checkpoint.active_segment);
    printf("Current write offset: %d\n\n", checkpoint.active_offset);

    // Step 1: Get all live inode locations from imap
    struct location live_inodes[10000];
    uint32_t live_count = 0;

    // Collect all live inodes
    for (uint32_t i = 0; i <= checkpoint.next_inode_num; i++)
    {
        struct location loc = imap_lookup(i);
        if (loc.segment_id != 0xFFFFFFFF)
            live_inodes[live_count++] = loc;
    }
    printf("Live inodes: %u\n", live_count);

    // Step 2: For each live inode, collect all its data blocks
    struct location live_blocks[100000];
    uint32_t block_count = 0;

    for (uint32_t i = 0; i < live_count; i++)
    {
        struct inode inode;
        fs_read(&live_inodes[i], &inode, sizeof(inode));
        uint32_t num_blocks = (inode.size + BLOCK_SIZE - 1) / BLOCK_SIZE;

        for (uint32_t b = 0; b < num_blocks; b++)
        {
            struct location block_loc;
            if (inode_get_block_location(&inode, b, &block_loc) &&
                block_loc.segment_id != 0xFFFFFFFF)
                live_blocks[block_count++] = block_loc;
        }
    }
    printf("Live blocks: %u\n", block_count);

    // Step 3: Copy live data to new segments
    struct location new_inode_locations[10000];
    for (uint32_t i = 0; i < live_count; i++)
    {
        struct inode inode;
        fs_read(&live_inodes[i], &inode, sizeof(inode));
        fs_append(&inode, sizeof(inode), &new_inode_locations[i]);
    }

    // Step 4: Copy all live data blocks to new log location
    struct location new_block_locations[100000];
    for (uint32_t i = 0; i < block_count; i++)
    {
        char buffer[BLOCK_SIZE];
        fs_read(&live_blocks[i], buffer, BLOCK_SIZE);
        fs_append(buffer, BLOCK_SIZE, &new_block_locations[i]);
    }

    // Step 5: Update inodes with new block locations
    // Rebuild mapping from old to new locations
    // For simplicity, we update each inode's block pointers
    uint32_t block_idx = 0;
    for (uint32_t i = 0; i < live_count; i++)
    {
        struct inode inode;
        fs_read(&live_inodes[i], &inode, sizeof(inode));
        uint32_t num_blocks = (inode.size + BLOCK_SIZE - 1) / BLOCK_SIZE;

        // Create new inode with updated block pointers
        struct inode new_inode;
        memcpy(&new_inode, &inode, sizeof(inode));

        // Reset block pointers
        for (int j = 0; j < 10; j++)
            new_inode.direct[j].segment_id = 0xFFFFFFFF;
        new_inode.single_indirect.segment_id = 0xFFFFFFFF;
        new_inode.double_indirect.segment_id = 0xFFFFFFFF;
        new_inode.triple_indirect.segment_id = 0xFFFFFFFF;

        // Set new block pointers (creates indirect blocks as needed)
        for (uint32_t b = 0; b < num_blocks; b++)
            inode_set_block_location(&new_inode, b, &new_block_locations[block_idx++]);

        // Write updated inode
        inode_write(i, &new_inode);
    }

    // Step 6: Delete old segment files (keep current active)
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

            // Don't delete the new active segment(s)
            if (seg_id != checkpoint.active_segment)
            {
                char path[100];
                sprintf(path, "segments/%s", entry->d_name);
                remove(path);
            }
        }
        closedir(dir);
    }

    // Step 7: Flush imap and update checkpoint
    imap_flush();
    checkpoint.imap_location = imap_get_current_location();
    fs_write_checkpoint();

    printf("----- CLEANER COMPLETE -----\n");
}

// Debug mode - shows inode details, block pointers, directory entries (-D)
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

    // Print direct block pointers
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

    // If directory, list its contents
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

    // Print location in log
    struct location loc = imap_lookup(inode_num);
    printf("\nLog location: seg %d, off %d\n\n", loc.segment_id, loc.offset);
}
