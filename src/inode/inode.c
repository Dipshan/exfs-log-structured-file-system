#include "inode.h"
#include "../fs/fs.h"
#include "../imap/imap.h"
#include "../utils/utils.h"

// Simple counter for new inode numbers
static uint32_t next_inode = 1;

// Create a new inode (file or directory)
void inode_create(uint32_t *inode_num, uint32_t type)
{
    *inode_num = next_inode++;

    struct inode inode;
    memset(&inode, 0, sizeof(inode));
    inode.type = type;
    inode.size = 0;

    // Initialize all locations to invalid (segment_id = 0xFFFFFFFF means invalid)
    for (int i = 0; i < 10; i++)
    {
        inode.direct[i].segment_id = 0xFFFFFFFF;
        inode.direct[i].offset = 0;
    }
    inode.single_indirect.segment_id = 0xFFFFFFFF;
    inode.double_indirect.segment_id = 0xFFFFFFFF;
    inode.triple_indirect.segment_id = 0xFFFFFFFF;

    struct location loc;
    fs_append(&inode, sizeof(inode), &loc);

    imap_update(*inode_num, &loc);
}

// Read an inode from disk into memory
void inode_read(uint32_t inode_num, struct inode *inode)
{
    struct location loc = imap_lookup(inode_num);
    if (loc.segment_id == 0xFFFFFFFF)
    {
        printf("ERROR: Inode %d not found\n", inode_num);
        return;
    }

    fs_read(&loc, inode, sizeof(struct inode));
}

// Write an updated inode back to disk (appends new version)
void inode_write(uint32_t inode_num, struct inode *inode)
{
    struct location new_loc;
    fs_append(inode, sizeof(struct inode), &new_loc);
    imap_update(inode_num, &new_loc);
}

// Add an entry to a directory
void dir_add_entry(uint32_t dir_inode, const char *name, uint32_t child_inode)
{
    struct inode dir;
    inode_read(dir_inode, &dir);

    if (dir.type != TYPE_DIRECTORY)
    {
        printf("ERROR: Inode %d is not a directory\n", dir_inode);
        return;
    }

    // Read existing directory entries
    struct directory_entry entries[DIR_ENTRIES_PER_BLOCK];
    memset(entries, 0, sizeof(entries));

    if (dir.direct[0].segment_id != 0xFFFFFFFF)
    {
        fs_read(&dir.direct[0], entries, BLOCK_SIZE);
    }

    // Find an empty slot
    int slot = -1;
    for (int i = 0; i < DIR_ENTRIES_PER_BLOCK; i++)
    {
        if (entries[i].inode_num == 0)
        {
            slot = i;
            break;
        }
    }

    if (slot == -1)
    {
        printf("ERROR: Directory is full (max %d entries)\n", DIR_ENTRIES_PER_BLOCK);
        return;
    }

    // Add the new entry
    strncpy(entries[slot].name, name, 255);
    entries[slot].name[255] = '\0';
    entries[slot].inode_num = child_inode;

    // Write updated directory data block
    struct location new_data_loc;
    fs_append(entries, BLOCK_SIZE, &new_data_loc);

    // Update directory inode to point to new data block
    dir.direct[0] = new_data_loc;
    inode_write(dir_inode, &dir);
}

// Remove an entry from a directory
void dir_remove_entry(uint32_t dir_inode, const char *name)
{
    struct inode dir;
    inode_read(dir_inode, &dir);

    if (dir.type != TYPE_DIRECTORY)
    {
        printf("ERROR: Inode %d is not a directory\n", dir_inode);
        return;
    }

    if (dir.direct[0].segment_id == 0xFFFFFFFF)
    {
        return; // Empty directory
    }

    // Read directory entries
    struct directory_entry entries[DIR_ENTRIES_PER_BLOCK];
    fs_read(&dir.direct[0], entries, BLOCK_SIZE);

    // Find and remove the entry
    int found = 0;
    for (int i = 0; i < DIR_ENTRIES_PER_BLOCK; i++)
    {
        if (entries[i].inode_num != 0 && strcmp(entries[i].name, name) == 0)
        {
            entries[i].inode_num = 0;
            memset(entries[i].name, 0, 256);
            found = 1;
            break;
        }
    }

    if (!found)
    {
        printf("ERROR: Entry '%s' not found in directory\n", name);
        return;
    }

    // Write updated directory data block
    struct location new_data_loc;
    fs_append(entries, BLOCK_SIZE, &new_data_loc);

    // Update directory inode
    dir.direct[0] = new_data_loc;
    inode_write(dir_inode, &dir);
}

// Find an entry in a directory by name, returns inode number or 0
uint32_t dir_find_entry(uint32_t dir_inode, const char *name)
{
    struct inode dir;
    inode_read(dir_inode, &dir);

    if (dir.type != TYPE_DIRECTORY)
    {
        return 0;
    }

    if (dir.direct[0].segment_id == 0xFFFFFFFF)
    {
        return 0; // Empty directory
    }

    struct directory_entry entries[DIR_ENTRIES_PER_BLOCK];
    fs_read(&dir.direct[0], entries, BLOCK_SIZE);

    for (int i = 0; i < DIR_ENTRIES_PER_BLOCK; i++)
    {
        if (entries[i].inode_num != 0 && strcmp(entries[i].name, name) == 0)
        {
            return entries[i].inode_num;
        }
    }

    return 0;
}

// Recursively list directory contents with indentation
void dir_list_recursive(uint32_t dir_inode, int depth)
{
    struct inode dir;
    inode_read(dir_inode, &dir);

    if (dir.type != TYPE_DIRECTORY)
    {
        return;
    }

    if (dir.direct[0].segment_id == 0xFFFFFFFF)
    {
        return; // Empty directory
    }

    struct directory_entry entries[DIR_ENTRIES_PER_BLOCK];
    fs_read(&dir.direct[0], entries, BLOCK_SIZE);

    for (int i = 0; i < DIR_ENTRIES_PER_BLOCK; i++)
    {
        if (entries[i].inode_num != 0)
        {
            // Print indentation
            for (int d = 0; d < depth; d++)
            {
                printf("  ");
            }

            struct inode child;
            inode_read(entries[i].inode_num, &child);

            if (child.type == TYPE_DIRECTORY)
            {
                printf("📁 %s/ (inode %d)\n", entries[i].name, entries[i].inode_num);
                dir_list_recursive(entries[i].inode_num, depth + 1);
            }
            else
            {
                printf("📄 %s (inode %d, %d bytes)\n", entries[i].name, entries[i].inode_num, child.size);
            }
        }
    }
}

// Get the location of a block (handles indirect blocks)
int inode_get_block_location(struct inode *inode, uint32_t block_num, struct location *loc)
{
    // Direct blocks (0-9)
    if (block_num < 10)
    {
        *loc = inode->direct[block_num];
        return (loc->segment_id != 0xFFFFFFFF);
    }

    block_num -= 10;

    // Single indirect block (10 to 10+POINTERS_PER_BLOCK-1)
    if (block_num < POINTERS_PER_BLOCK)
    {
        if (inode->single_indirect.segment_id == 0xFFFFFFFF)
        {
            return 0;
        }

        // Read the indirect block
        struct location indirect_block[POINTERS_PER_BLOCK];
        fs_read(&inode->single_indirect, indirect_block, BLOCK_SIZE);

        *loc = indirect_block[block_num];
        return (loc->segment_id != 0xFFFFFFFF);
    }

    block_num -= POINTERS_PER_BLOCK;

    // Double indirect block
    uint32_t first_level = block_num / POINTERS_PER_BLOCK;
    uint32_t second_level = block_num % POINTERS_PER_BLOCK;

    if (first_level < POINTERS_PER_BLOCK)
    {
        if (inode->double_indirect.segment_id == 0xFFFFFFFF)
        {
            return 0;
        }

        // Read first level indirect block
        struct location first_level_block[POINTERS_PER_BLOCK];
        fs_read(&inode->double_indirect, first_level_block, BLOCK_SIZE);

        if (first_level_block[first_level].segment_id == 0xFFFFFFFF)
        {
            return 0;
        }

        // Read second level indirect block
        struct location second_level_block[POINTERS_PER_BLOCK];
        fs_read(&first_level_block[first_level], second_level_block, BLOCK_SIZE);

        *loc = second_level_block[second_level];
        return (loc->segment_id != 0xFFFFFFFF);
    }

    block_num -= (POINTERS_PER_BLOCK * POINTERS_PER_BLOCK);

    // Triple indirect block
    uint32_t l1 = block_num / (POINTERS_PER_BLOCK * POINTERS_PER_BLOCK);
    uint32_t rem = block_num % (POINTERS_PER_BLOCK * POINTERS_PER_BLOCK);
    uint32_t l2 = rem / POINTERS_PER_BLOCK;
    uint32_t l3 = rem % POINTERS_PER_BLOCK;

    if (l1 < POINTERS_PER_BLOCK)
    {
        if (inode->triple_indirect.segment_id == 0xFFFFFFFF)
        {
            return 0;
        }

        // Read level 1
        struct location l1_block[POINTERS_PER_BLOCK];
        fs_read(&inode->triple_indirect, l1_block, BLOCK_SIZE);

        if (l1_block[l1].segment_id == 0xFFFFFFFF)
        {
            return 0;
        }

        // Read level 2
        struct location l2_block[POINTERS_PER_BLOCK];
        fs_read(&l1_block[l1], l2_block, BLOCK_SIZE);

        if (l2_block[l2].segment_id == 0xFFFFFFFF)
        {
            return 0;
        }

        // Read level 3
        struct location l3_block[POINTERS_PER_BLOCK];
        fs_read(&l2_block[l2], l3_block, BLOCK_SIZE);

        *loc = l3_block[l3];
        return (loc->segment_id != 0xFFFFFFFF);
    }

    return 0; // Block number out of range
}

// Set the location of a block (creates indirect blocks as needed)
int inode_set_block_location(struct inode *inode, uint32_t block_num, struct location *loc)
{
    // Direct blocks (0-9)
    if (block_num < 10)
    {
        inode->direct[block_num] = *loc;
        return 1;
    }

    block_num -= 10;

    // Single indirect block
    if (block_num < POINTERS_PER_BLOCK)
    {
        // Create single indirect block if it doesn't exist
        if (inode->single_indirect.segment_id == 0xFFFFFFFF)
        {
            struct location empty_block[POINTERS_PER_BLOCK];
            memset(empty_block, 0xFF, sizeof(empty_block));
            fs_append(empty_block, BLOCK_SIZE, &inode->single_indirect);
        }

        // Read, modify, write indirect block
        struct location indirect_block[POINTERS_PER_BLOCK];
        fs_read(&inode->single_indirect, indirect_block, BLOCK_SIZE);
        indirect_block[block_num] = *loc;
        fs_append(indirect_block, BLOCK_SIZE, &inode->single_indirect);
        return 1;
    }

    block_num -= POINTERS_PER_BLOCK;

    // Double indirect block
    uint32_t first_level = block_num / POINTERS_PER_BLOCK;
    uint32_t second_level = block_num % POINTERS_PER_BLOCK;

    if (first_level < POINTERS_PER_BLOCK)
    {
        // Create double indirect block if it doesn't exist
        if (inode->double_indirect.segment_id == 0xFFFFFFFF)
        {
            struct location empty_block[POINTERS_PER_BLOCK];
            memset(empty_block, 0xFF, sizeof(empty_block));
            fs_append(empty_block, BLOCK_SIZE, &inode->double_indirect);
        }

        // Read first level
        struct location first_level_block[POINTERS_PER_BLOCK];
        fs_read(&inode->double_indirect, first_level_block, BLOCK_SIZE);

        // Create second level if needed
        if (first_level_block[first_level].segment_id == 0xFFFFFFFF)
        {
            struct location empty_block[POINTERS_PER_BLOCK];
            memset(empty_block, 0xFF, sizeof(empty_block));
            fs_append(empty_block, BLOCK_SIZE, &first_level_block[first_level]);
        }

        // Read and update second level
        struct location second_level_block[POINTERS_PER_BLOCK];
        fs_read(&first_level_block[first_level], second_level_block, BLOCK_SIZE);
        second_level_block[second_level] = *loc;
        fs_append(second_level_block, BLOCK_SIZE, &first_level_block[first_level]);

        // Update first level
        fs_append(first_level_block, BLOCK_SIZE, &inode->double_indirect);
        return 1;
    }

    block_num -= (POINTERS_PER_BLOCK * POINTERS_PER_BLOCK);

    // Triple indirect block
    uint32_t l1 = block_num / (POINTERS_PER_BLOCK * POINTERS_PER_BLOCK);
    uint32_t rem = block_num % (POINTERS_PER_BLOCK * POINTERS_PER_BLOCK);
    uint32_t l2 = rem / POINTERS_PER_BLOCK;
    uint32_t l3 = rem % POINTERS_PER_BLOCK;

    if (l1 < POINTERS_PER_BLOCK)
    {
        // Create triple indirect block if it doesn't exist
        if (inode->triple_indirect.segment_id == 0xFFFFFFFF)
        {
            struct location empty_block[POINTERS_PER_BLOCK];
            memset(empty_block, 0xFF, sizeof(empty_block));
            fs_append(empty_block, BLOCK_SIZE, &inode->triple_indirect);
        }

        // Read level 1
        struct location l1_block[POINTERS_PER_BLOCK];
        fs_read(&inode->triple_indirect, l1_block, BLOCK_SIZE);

        // Create level 2 if needed
        if (l1_block[l1].segment_id == 0xFFFFFFFF)
        {
            struct location empty_block[POINTERS_PER_BLOCK];
            memset(empty_block, 0xFF, sizeof(empty_block));
            fs_append(empty_block, BLOCK_SIZE, &l1_block[l1]);
        }

        // Read level 2
        struct location l2_block[POINTERS_PER_BLOCK];
        fs_read(&l1_block[l1], l2_block, BLOCK_SIZE);

        // Create level 3 if needed
        if (l2_block[l2].segment_id == 0xFFFFFFFF)
        {
            struct location empty_block[POINTERS_PER_BLOCK];
            memset(empty_block, 0xFF, sizeof(empty_block));
            fs_append(empty_block, BLOCK_SIZE, &l2_block[l2]);
        }

        // Read and update level 3
        struct location l3_block[POINTERS_PER_BLOCK];
        fs_read(&l2_block[l2], l3_block, BLOCK_SIZE);
        l3_block[l3] = *loc;
        fs_append(l3_block, BLOCK_SIZE, &l2_block[l2]);

        // Update level 2
        fs_append(l2_block, BLOCK_SIZE, &l1_block[l1]);

        // Update level 1
        fs_append(l1_block, BLOCK_SIZE, &inode->triple_indirect);
        return 1;
    }

    return 0; // Block number out of range
}