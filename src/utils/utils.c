/**
 * ExFS-Log: Log-Structured File System
 * * utils.c
 * Implements safe file stream wrappers to guarantee precise byte ingestion,
 * and handles recursive path tokenization for directory tree traversal.
 */

#include "utils.h"
#include "../fs/fs.h"
#include "../inode/inode.h"
#include "../imap/imap.h"

/**
 * safe_read
 * Reads exactly 'bytes' from the file stream into the buffer.
 * Defensively terminates the program if an I/O error or EOF prevents a full read.
 */
void safe_read(FILE *f, void *buffer, size_t bytes)
{
    size_t n = fread(buffer, 1, bytes, f);
    if (n != bytes)
    {
        printf("ERROR: Failed to read %zu bytes (got %zu)\n", bytes, n);
        exit(1);
    }
}

/**
 * safe_write
 * Writes exactly 'bytes' from the buffer to the file stream.
 * Defensively terminates the program if an I/O error prevents a full write.
 */
void safe_write(FILE *f, void *buffer, size_t bytes)
{
    size_t n = fwrite(buffer, 1, bytes, f);
    if (n != bytes)
    {
        printf("ERROR: Failed to write %zu bytes (wrote %zu)\n", bytes, n);
        exit(1);
    }
}

/**
 * split_path
 * Parses a full path (e.g., "/docs/folder/file.txt") into its parent path 
 * ("/docs/folder") and its final target name ("file.txt").
 * Uses strncpy to defensively prevent buffer overflows.
 */
void split_path(const char *path, char *parent, char *name)
{
    const char *last_slash = strrchr(path, '/');

    // Case: No slashes found (e.g., "file.txt" in current directory)
    if (!last_slash)
    {
        strcpy(parent, ".");
        strncpy(name, path, 251);
        name[251] = '\0';
        return;
    }

    // Copy everything before the last slash into 'parent'
    size_t parent_len = last_slash - path;
    if (parent_len == 0)
    {
        strcpy(parent, "/"); // The parent is exactly the root
    }
    else
    {
        // Defensively truncate to avoid buffer overflows (assuming 1024 char limit)
        size_t copy_len = (parent_len < 1023) ? parent_len : 1023;
        strncpy(parent, path, copy_len);
        parent[copy_len] = '\0';
    }

    // Copy everything after the last slash into 'name'
    strncpy(name, last_slash + 1, 251);
    name[251] = '\0';
}

/**
 * find_inode
 * Traverses the directory tree starting from the root to find the Inode ID
 * associated with the provided path. Returns 0 if the path is invalid.
 */
uint32_t find_inode(const char *path)
{
    // Root directory is always rigidly assigned to Inode 0
    if (strcmp(path, "/") == 0)
    {
        return 0;
    }

    // Create a modifiable, memory-safe copy of the path for tokenization
    char temp[1024];
    strncpy(temp, path, 1023);
    temp[1023] = '\0';

    uint32_t current_inode = 0;
    char *token = strtok(temp, "/");

    // Walk the directory tree token by token
    while (token)
    {
        uint32_t next_inode = dir_find_entry(current_inode, token);
        if (next_inode == 0)
            return 0; // Traversal failed; path does not exist
        
        current_inode = next_inode;
        token = strtok(NULL, "/");
    }

    return current_inode;
}

/**
 * path_exists
 * Boolean wrapper. Returns 1 if the path corresponds to a valid Inode, 0 otherwise.
 */
int path_exists(const char *path)
{
    // find_inode returns 0 for root or invalid. Since root exists, handle edge case:
    if (strcmp(path, "/") == 0) return 1;
    return find_inode(path) != 0;
}

/**
 * create_parent_dirs
 * Recursively traverses a path backward, identifying missing directories 
 * and generating new directory Inodes to satisfy the required path structure.
 */
void create_parent_dirs(const char *path)
{
    char parent[1024], name[256];
    split_path(path, parent, name);

    // Base case: We have reached the root or current directory
    if (strcmp(parent, "/") == 0 || strcmp(parent, ".") == 0)
        return;

    // If the parent directory is missing, recursively build it first
    if (!path_exists(parent))
    {
        create_parent_dirs(parent);

        // Once the grandparent exists, we can link the new parent into it
        char grand_parent[1024], dir_name[256];
        split_path(parent, grand_parent, dir_name);

        uint32_t gp_inode = find_inode(grand_parent);

        // Grandparent is permitted to be 0 ONLY if it is the root directory
        if (gp_inode == 0 && strcmp(grand_parent, "/") != 0)
        {
            printf("ERROR: Could not find/create parent %s\n", grand_parent);
            return;
        }

        // Generate the missing directory Inode and map it to the grandparent
        uint32_t new_dir_inode;
        inode_create(&new_dir_inode, TYPE_DIRECTORY);
        dir_add_entry(gp_inode, dir_name, new_dir_inode);
    }
}