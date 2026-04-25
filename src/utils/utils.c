#include "utils.h"
#include "../fs/fs.h"
#include "../inode/inode.h"
#include "../imap/imap.h"

// Read exactly 'bytes' from file, exit if fails
void safe_read(FILE *f, void *buffer, size_t bytes)
{
    size_t n = fread(buffer, 1, bytes, f);
    if (n != bytes)
    {
        printf("ERROR: Failed to read %zu bytes (got %zu)\n", bytes, n);
        exit(1);
    }
}

// Write exactly 'bytes' to file, exit if fails
void safe_write(FILE *f, void *buffer, size_t bytes)
{
    size_t n = fwrite(buffer, 1, bytes, f);
    if (n != bytes)
    {
        printf("ERROR: Failed to write %zu bytes (wrote %zu)\n", bytes, n);
        exit(1);
    }
}

// Split path into parent directory and last component
void split_path(const char *path, char *parent, char *name)
{
    const char *last_slash = strrchr(path, '/');

    if (!last_slash)
    {
        strcpy(parent, ".");
        strcpy(name, path);
        return;
    }

    // Copy everything before last slash
    size_t parent_len = last_slash - path;
    if (parent_len == 0)
    {
        strcpy(parent, "/");
    }
    else
    {
        strncpy(parent, path, parent_len);
        parent[parent_len] = '\0';
    }

    // Copy everything after last slash
    strcpy(name, last_slash + 1);
}

// Find inode number for a given path
uint32_t find_inode(const char *path)
{
    // Root is always inode 0
    if (strcmp(path, "/") == 0)
    {
        return 0;
    }

    char temp[1024];
    strcpy(temp, path);

    uint32_t current_inode = 0;
    char *token = strtok(temp, "/");

    while (token)
    {
        uint32_t next_inode = dir_find_entry(current_inode, token);
        if (next_inode == 0)
        {
            return 0; // Not found
        }
        current_inode = next_inode;
        token = strtok(NULL, "/");
    }

    return current_inode;
}

// Check if a path exists
int path_exists(const char *path)
{
    return find_inode(path) != 0;
}

// Create all parent directories for a path if they don't exist
void create_parent_dirs(const char *path)
{
    char parent[1024];
    char name[256];
    split_path(path, parent, name);

    // No parent directories to create
    if (strcmp(parent, "/") == 0 || strcmp(parent, ".") == 0)
    {
        return;
    }

    // If parent doesn't exist, create it
    if (!path_exists(parent))
    {
        create_parent_dirs(parent);

        // FIX: We need the inode of the GRANDPARENT to create the parent directory
        char grand_parent[1024];
        char dir_name[256];
        split_path(parent, grand_parent, dir_name);

        uint32_t gp_inode = find_inode(grand_parent);
        
        // FIX: We must allow gp_inode to be 0 IF the grandparent is the root directory ("/")
        if (gp_inode == 0 && strcmp(grand_parent, "/") != 0)
        {
            printf("ERROR: Could not find/create parent %s\n", grand_parent);
            return;
        }

        uint32_t new_dir_inode;
        inode_create(&new_dir_inode, TYPE_DIRECTORY);

        // Add the new directory to its grandparent
        dir_add_entry(gp_inode, dir_name, new_dir_inode);
    }
}