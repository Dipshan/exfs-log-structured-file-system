// Filename: utils.h
// Provides defensive I/O wrappers and path resolution utilities.

#ifndef UTILS_H
#define UTILS_H

#include "../common.h" // For location struct

// Reads exactly 'bytes' from file, exits on failure
void safe_read(FILE *f, void *buffer, size_t bytes);

// Writes exactly 'bytes' to file, exits on failure
void safe_write(FILE *f, void *buffer, size_t bytes);

// Splits "/a/b/c.txt" into parent="/a/b" and name="c.txt"
void split_path(const char *path, char *parent, char *name);

// Walks path from root, returns inode number or 0 if not found
uint32_t find_inode(const char *path);

// Returns 1 if path exists, 0 otherwise
int path_exists(const char *path);

// Creates all missing parent directories for a given path
void create_parent_dirs(const char *path);

#endif