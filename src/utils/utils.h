#ifndef UTILS_H
#define UTILS_H

#include "../common.h"

// Read exactly bytes from file, exit if error
void safe_read(FILE *f, void *buffer, size_t bytes);

// Write exactly bytes to file, exit if error
void safe_write(FILE *f, void *buffer, size_t bytes);

// Split path into parent directory and last component
void split_path(const char *path, char *parent, char *name);

// Find inode number for a path, returns 0 if not found
uint32_t find_inode(const char *path);

// Check if path exists, returns 1 if yes
int path_exists(const char *path);

// Create all parent directories if they don't exist
void create_parent_dirs(const char *path);

#endif