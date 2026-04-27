/**
 * ExFS-Log: Log-Structured File System
 * * utils.h
 * Provides defensive I/O wrappers and path resolution utilities.
 */

#ifndef UTILS_H
#define UTILS_H

#include "../common.h"

/**
 * Reads exactly the requested number of bytes from a file stream.
 * Exits the program if the read is incomplete.
 */
void safe_read(FILE *f, void *buffer, size_t bytes);

/**
 * Writes exactly the requested number of bytes to a file stream.
 * Exits the program if the write is incomplete.
 */
void safe_write(FILE *f, void *buffer, size_t bytes);

/**
 * Splits a full file path into its parent directory and final component name.
 */
void split_path(const char *path, char *parent, char *name);

/**
 * Traverses the file system tree to locate the Inode number for a given path.
 */
uint32_t find_inode(const char *path);

/**
 * Checks if a specific path exists within the file system.
 */
int path_exists(const char *path);

/**
 * Recursively creates all missing parent directories for a given path.
 */
void create_parent_dirs(const char *path);

#endif