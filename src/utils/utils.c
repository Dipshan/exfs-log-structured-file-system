// Filename: utils.c
// Implements safe I/O wrappers, path splitting, directory traversal, and auto-creation of parent directories.

#include "utils.h"          // For function declarations
#include "../fs/fs.h"       // For fs_read, fs_append
#include "../inode/inode.h" // For inode_create, dir_find_entry, dir_add_entry
#include "../imap/imap.h"   // For imap_lookup

// Reads exactly 'bytes' from file, exits on failure
void safe_read(FILE *f, void *buffer, size_t bytes)
{
    size_t n = fread(buffer, 1, bytes, f);
    if (n != bytes)
    {
        printf("ERROR: Failed to read %zu bytes (got %zu)\n", bytes, n);
        exit(1);
    }
}

// Writes exactly 'bytes' to file, exits on failure
void safe_write(FILE *f, void *buffer, size_t bytes)
{
    size_t n = fwrite(buffer, 1, bytes, f);
    if (n != bytes)
    {
        printf("ERROR: Failed to write %zu bytes (wrote %zu)\n", bytes, n);
        exit(1);
    }
}

// Splits "/a/b/c.txt" into parent="/a/b" and name="c.txt"
void split_path(const char *path, char *parent, char *name)
{
    const char *last_slash = strrchr(path, '/');

    // No slash - treat as current directory
    if (!last_slash)
    {
        strcpy(parent, ".");
        strncpy(name, path, 251);
        name[251] = '\0';
        return;
    }

    // Copy everything before last slash into parent
    size_t parent_len = last_slash - path;
    if (parent_len == 0)
    {
        strcpy(parent, "/");
    }
    else
    {
        size_t copy_len = (parent_len < 1023) ? parent_len : 1023;
        strncpy(parent, path, copy_len);
        parent[copy_len] = '\0';
    }

    // Copy everything after last slash into name
    strncpy(name, last_slash + 1, 251);
    name[251] = '\0';
}

// Walks path from root, returns inode number or 0 if not found
uint32_t find_inode(const char *path)
{
    // Root is always inode 0
    if (strcmp(path, "/") == 0)
    {
        return 0;
    }

    // Create modifiable copy of path
    char temp[1024];
    strncpy(temp, path, 1023);
    temp[1023] = '\0';

    uint32_t current_inode = 0;
    char *token = strtok(temp, "/");

    // Walk each component of the path
    while (token)
    {
        uint32_t next_inode = dir_find_entry(current_inode, token);
        if (next_inode == 0)
            return 0; // Path component not found

        current_inode = next_inode;
        token = strtok(NULL, "/");
    }

    return current_inode;
}

// Returns 1 if path exists, 0 otherwise
int path_exists(const char *path)
{
    if (strcmp(path, "/") == 0)
        return 1;
    return find_inode(path) != 0;
}

// Creates all missing parent directories for a given path
void create_parent_dirs(const char *path)
{
    char parent[1024], name[256];
    split_path(path, parent, name);

    // Reached root - stop
    if (strcmp(parent, "/") == 0 || strcmp(parent, ".") == 0)
        return;

    // If parent doesn't exist, create it recursively
    if (!path_exists(parent))
    {
        create_parent_dirs(parent);

        // Now create the immediate parent directory
        char grand_parent[1024], dir_name[256];
        split_path(parent, grand_parent, dir_name);

        uint32_t gp_inode = find_inode(grand_parent);

        // Grandparent must exist (or be root)
        if (gp_inode == 0 && strcmp(grand_parent, "/") != 0)
        {
            printf("ERROR: Could not find/create parent %s\n", grand_parent);
            return;
        }

        // Create new directory and add to grandparent
        uint32_t new_dir_inode;
        inode_create(&new_dir_inode, TYPE_DIRECTORY);
        dir_add_entry(gp_inode, dir_name, new_dir_inode);
    }
}