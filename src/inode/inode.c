/**
 * ExFS-Log: Log-Structured File System
 * * inode.c
 * Manages the allocation, modification, and persistence of Inodes.
 * Implements the LFS Cascading Copy-on-Write mechanism for indirect 
 * block pointer traversal and directory entry management.
 */

#include "inode.h"
#include "../fs/fs.h"
#include "../imap/imap.h"
#include "../utils/utils.h"

/**
 * Allocates a new Inode by dynamically scanning the Imap for the next 
 * available ID, ensuring persistence and preventing overwrite collisions 
 * after a system reboot.
 */
void inode_create(uint32_t *inode_num, uint32_t type)
{
    uint32_t next_inode = 1;
    while (imap_lookup(next_inode).segment_id != 0xFFFFFFFF)
    {
        next_inode++;
    }
    *inode_num = next_inode;

    struct inode inode;
    memset(&inode, 0, sizeof(inode));
    inode.type = type;
    inode.size = 0;

    // Initialize all pointers to an invalid state (0xFFFFFFFF)
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

/**
 * In an LFS, modifying an inode means writing a completely new version 
 * to the tail of the log and updating the Imap.
 */
void inode_write(uint32_t inode_num, struct inode *inode)
{
    struct location new_loc;
    fs_append(inode, sizeof(struct inode), &new_loc);
    imap_update(inode_num, &new_loc);
}

void dir_add_entry(uint32_t dir_inode, const char *name, uint32_t child_inode)
{
    struct inode dir;
    inode_read(dir_inode, &dir);

    if (dir.type != TYPE_DIRECTORY)
    {
        printf("ERROR: Inode %d is not a directory\n", dir_inode);
        return;
    }

    struct directory_entry entries[DIR_ENTRIES_PER_BLOCK];
    memset(entries, 0, sizeof(entries));

    if (dir.direct[0].segment_id != 0xFFFFFFFF)
        fs_read(&dir.direct[0], entries, BLOCK_SIZE);

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

    strncpy(entries[slot].name, name, 251);
    entries[slot].name[251] = '\0';
    entries[slot].inode_num = child_inode;

    struct location new_data_loc;
    fs_append(entries, BLOCK_SIZE, &new_data_loc);

    dir.direct[0] = new_data_loc;
    inode_write(dir_inode, &dir);
}

void dir_remove_entry(uint32_t dir_inode, const char *name)
{
    struct inode dir;
    inode_read(dir_inode, &dir);

    if (dir.type != TYPE_DIRECTORY || dir.direct[0].segment_id == 0xFFFFFFFF)
        return;

    struct directory_entry entries[DIR_ENTRIES_PER_BLOCK];
    fs_read(&dir.direct[0], entries, BLOCK_SIZE);

    int found = 0;
    for (int i = 0; i < DIR_ENTRIES_PER_BLOCK; i++)
    {
        if (entries[i].inode_num != 0 && strcmp(entries[i].name, name) == 0)
        {
            entries[i].inode_num = 0;
            memset(entries[i].name, 0, 252);
            found = 1;
            break;
        }
    }

    if (!found)
    {
        printf("ERROR: Entry '%s' not found in directory\n", name);
        return;
    }

    struct location new_data_loc;
    fs_append(entries, BLOCK_SIZE, &new_data_loc);

    dir.direct[0] = new_data_loc;
    inode_write(dir_inode, &dir);
}

uint32_t dir_find_entry(uint32_t dir_inode, const char *name)
{
    struct inode dir;
    inode_read(dir_inode, &dir);

    if (dir.type != TYPE_DIRECTORY || dir.direct[0].segment_id == 0xFFFFFFFF)
        return 0;

    struct directory_entry entries[DIR_ENTRIES_PER_BLOCK];
    fs_read(&dir.direct[0], entries, BLOCK_SIZE);

    for (int i = 0; i < DIR_ENTRIES_PER_BLOCK; i++)
    {
        if (entries[i].inode_num != 0 && strcmp(entries[i].name, name) == 0)
            return entries[i].inode_num;
    }

    return 0;
}

void dir_list_recursive(uint32_t dir_inode, int depth)
{
    struct inode dir;
    inode_read(dir_inode, &dir);

    if (dir.type != TYPE_DIRECTORY || dir.direct[0].segment_id == 0xFFFFFFFF)
        return;

    struct directory_entry entries[DIR_ENTRIES_PER_BLOCK];
    fs_read(&dir.direct[0], entries, BLOCK_SIZE);

    for (int i = 0; i < DIR_ENTRIES_PER_BLOCK; i++)
    {
        if (entries[i].inode_num != 0)
        {
            for (int d = 0; d < depth; d++)
                printf("  ");

            struct inode child;
            inode_read(entries[i].inode_num, &child);

            if (child.type == TYPE_DIRECTORY)
            {
                printf("%s/ (inode %d)\n", entries[i].name, entries[i].inode_num);
                dir_list_recursive(entries[i].inode_num, depth + 1);
            }
            else
            {
                printf("%s (inode %d, %d bytes)\n", entries[i].name, entries[i].inode_num, child.size);
            }
        }
    }
}

/**
 * Resolves logical block indices into physical LFS locations.
 * Traverses the indirect block pointer tree (O(1) to O(4) depth).
 */
int inode_get_block_location(struct inode *inode, uint32_t block_num, struct location *loc)
{
    // Direct Blocks (0-9)
    if (block_num < 10)
    {
        *loc = inode->direct[block_num];
        return (loc->segment_id != 0xFFFFFFFF);
    }
    block_num -= 10;

    // Single Indirect
    if (block_num < POINTERS_PER_BLOCK)
    {
        if (inode->single_indirect.segment_id == 0xFFFFFFFF) return 0;

        struct location indirect_block[POINTERS_PER_BLOCK];
        fs_read(&inode->single_indirect, indirect_block, BLOCK_SIZE);
        *loc = indirect_block[block_num];
        return (loc->segment_id != 0xFFFFFFFF);
    }
    block_num -= POINTERS_PER_BLOCK;

    // Double Indirect
    uint32_t first_level = block_num / POINTERS_PER_BLOCK;
    uint32_t second_level = block_num % POINTERS_PER_BLOCK;

    if (first_level < POINTERS_PER_BLOCK)
    {
        if (inode->double_indirect.segment_id == 0xFFFFFFFF) return 0;

        struct location first_level_block[POINTERS_PER_BLOCK];
        fs_read(&inode->double_indirect, first_level_block, BLOCK_SIZE);

        if (first_level_block[first_level].segment_id == 0xFFFFFFFF) return 0;

        struct location second_level_block[POINTERS_PER_BLOCK];
        fs_read(&first_level_block[first_level], second_level_block, BLOCK_SIZE);
        *loc = second_level_block[second_level];
        return (loc->segment_id != 0xFFFFFFFF);
    }
    block_num -= (POINTERS_PER_BLOCK * POINTERS_PER_BLOCK);

    // Triple Indirect
    uint32_t l1 = block_num / (POINTERS_PER_BLOCK * POINTERS_PER_BLOCK);
    uint32_t rem = block_num % (POINTERS_PER_BLOCK * POINTERS_PER_BLOCK);
    uint32_t l2 = rem / POINTERS_PER_BLOCK;
    uint32_t l3 = rem % POINTERS_PER_BLOCK;

    if (l1 < POINTERS_PER_BLOCK)
    {
        if (inode->triple_indirect.segment_id == 0xFFFFFFFF) return 0;

        struct location l1_block[POINTERS_PER_BLOCK];
        fs_read(&inode->triple_indirect, l1_block, BLOCK_SIZE);
        if (l1_block[l1].segment_id == 0xFFFFFFFF) return 0;

        struct location l2_block[POINTERS_PER_BLOCK];
        fs_read(&l1_block[l1], l2_block, BLOCK_SIZE);
        if (l2_block[l2].segment_id == 0xFFFFFFFF) return 0;

        struct location l3_block[POINTERS_PER_BLOCK];
        fs_read(&l2_block[l2], l3_block, BLOCK_SIZE);
        *loc = l3_block[l3];
        return (loc->segment_id != 0xFFFFFFFF);
    }
    return 0; 
}

/**
 * Cascading Copy-on-Write:
 * Assigns a physical address to a logical block. If an indirect tree is required, 
 * this function recursively spawns new pointer blocks, modifies them, and appends 
 * the newly modified parent blocks to the tail of the log to ensure LFS persistence.
 */
int inode_set_block_location(struct inode *inode, uint32_t block_num, struct location *loc)
{
    // Direct
    if (block_num < 10)
    {
        inode->direct[block_num] = *loc;
        return 1;
    }
    block_num -= 10;

    // Single Indirect
    if (block_num < POINTERS_PER_BLOCK)
    {
        if (inode->single_indirect.segment_id == 0xFFFFFFFF)
        {
            struct location empty_block[POINTERS_PER_BLOCK];
            memset(empty_block, 0xFF, sizeof(empty_block));
            fs_append(empty_block, BLOCK_SIZE, &inode->single_indirect);
        }

        struct location indirect_block[POINTERS_PER_BLOCK];
        fs_read(&inode->single_indirect, indirect_block, BLOCK_SIZE);
        indirect_block[block_num] = *loc;
        fs_append(indirect_block, BLOCK_SIZE, &inode->single_indirect);
        return 1;
    }
    block_num -= POINTERS_PER_BLOCK;

    // Double Indirect
    uint32_t first_level = block_num / POINTERS_PER_BLOCK;
    uint32_t second_level = block_num % POINTERS_PER_BLOCK;

    if (first_level < POINTERS_PER_BLOCK)
    {
        if (inode->double_indirect.segment_id == 0xFFFFFFFF)
        {
            struct location empty_block[POINTERS_PER_BLOCK];
            memset(empty_block, 0xFF, sizeof(empty_block));
            fs_append(empty_block, BLOCK_SIZE, &inode->double_indirect);
        }

        struct location first_level_block[POINTERS_PER_BLOCK];
        fs_read(&inode->double_indirect, first_level_block, BLOCK_SIZE);

        if (first_level_block[first_level].segment_id == 0xFFFFFFFF)
        {
            struct location empty_block[POINTERS_PER_BLOCK];
            memset(empty_block, 0xFF, sizeof(empty_block));
            fs_append(empty_block, BLOCK_SIZE, &first_level_block[first_level]);
        }

        struct location second_level_block[POINTERS_PER_BLOCK];
        fs_read(&first_level_block[first_level], second_level_block, BLOCK_SIZE);
        second_level_block[second_level] = *loc;
        fs_append(second_level_block, BLOCK_SIZE, &first_level_block[first_level]);
        fs_append(first_level_block, BLOCK_SIZE, &inode->double_indirect);
        return 1;
    }
    block_num -= (POINTERS_PER_BLOCK * POINTERS_PER_BLOCK);

    // Triple Indirect
    uint32_t l1 = block_num / (POINTERS_PER_BLOCK * POINTERS_PER_BLOCK);
    uint32_t rem = block_num % (POINTERS_PER_BLOCK * POINTERS_PER_BLOCK);
    uint32_t l2 = rem / POINTERS_PER_BLOCK;
    uint32_t l3 = rem % POINTERS_PER_BLOCK;

    if (l1 < POINTERS_PER_BLOCK)
    {
        if (inode->triple_indirect.segment_id == 0xFFFFFFFF)
        {
            struct location empty_block[POINTERS_PER_BLOCK];
            memset(empty_block, 0xFF, sizeof(empty_block));
            fs_append(empty_block, BLOCK_SIZE, &inode->triple_indirect);
        }

        struct location l1_block[POINTERS_PER_BLOCK];
        fs_read(&inode->triple_indirect, l1_block, BLOCK_SIZE);

        if (l1_block[l1].segment_id == 0xFFFFFFFF)
        {
            struct location empty_block[POINTERS_PER_BLOCK];
            memset(empty_block, 0xFF, sizeof(empty_block));
            fs_append(empty_block, BLOCK_SIZE, &l1_block[l1]);
        }

        struct location l2_block[POINTERS_PER_BLOCK];
        fs_read(&l1_block[l1], l2_block, BLOCK_SIZE);

        if (l2_block[l2].segment_id == 0xFFFFFFFF)
        {
            struct location empty_block[POINTERS_PER_BLOCK];
            memset(empty_block, 0xFF, sizeof(empty_block));
            fs_append(empty_block, BLOCK_SIZE, &l2_block[l2]);
        }

        struct location l3_block[POINTERS_PER_BLOCK];
        fs_read(&l2_block[l2], l3_block, BLOCK_SIZE);
        
        l3_block[l3] = *loc;
        
        fs_append(l3_block, BLOCK_SIZE, &l2_block[l2]);
        fs_append(l2_block, BLOCK_SIZE, &l1_block[l1]);
        fs_append(l1_block, BLOCK_SIZE, &inode->triple_indirect);
        return 1;
    }

    return 0; 
}