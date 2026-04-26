/**
 * ExFS-Log: Log-Structured File System
 * * main.c
 * Entry point for the ExFS command-line interface. Handles argument parsing,
 * system mounting, and routing commands to the core file system logic.
 * * Authors:
 * - Anuska Bhattarai (800832698) - anuskbh@siue.edu
 * - Deepshan Adhikari (800846035) - deepadh@siue.edu
 * - Sumit Shrestha (800835513) - sumishr@siue.edu
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

#include "fs/fs.h"
#include "utils/utils.h"
#include "imap/imap.h"

// ANSI color codes for terminal output
#define COLOR_RESET "\033[0m"
#define COLOR_RED "\033[31m"
#define COLOR_GREEN "\033[32m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_BLUE "\033[34m"
#define COLOR_CYAN "\033[36m"
#define COLOR_BOLD "\033[1m"

// Prints the system banner (Using safe ASCII characters for universal terminal compatibility)
void print_banner(void)
{
    fprintf(stderr, COLOR_GREEN);
    fprintf(stderr, "\n+----------------------------------------------------------+\n");
    fprintf(stderr, "|              EXFS-LOG-STRUCTURED FILE SYSTEM             |\n");
    fprintf(stderr, "|              Log-Structured File System (LFS)            |\n");
    fprintf(stderr, "+----------------------------------------------------------+\n");
    fprintf(stderr, COLOR_RESET "\n");
}

// Prints the help menu and CLI usage instructions
void print_usage(void)
{
    printf(COLOR_GREEN "USAGE:\n" COLOR_RESET);
    printf("  ./exfs-log-structured-file-system [OPTION] [ARGUMENTS]\n\n");

    printf(COLOR_GREEN "OPTIONS:\n" COLOR_RESET);
    printf("  %-30s %s\n", "-l", "List entire file system tree");
    printf("  %-30s %s\n", "-a /path -f /host/file", "Add file from host to file system");
    printf("  %-30s %s\n", "-r /path", "Remove file or directory");
    printf("  %-30s %s\n", "-e /path", "Extract file to stdout");
    printf("  %-30s %s\n", "-c", "Run cleaner to reclaim space");
    printf("  %-30s %s\n", "-D /path", "Debug mode - show inode details");
    printf("  %-30s %s\n", "-h, --help", "Show this help message");
    printf("  %-30s %s\n", "--init", "Initialize a fresh file system");

    printf(COLOR_GREEN "\nEXAMPLES:\n" COLOR_RESET);
    printf("  %-50s %s\n", "./exfs-log-structured-file-system -l", "Show all files and directories");
    printf("  %-50s %s\n", "./exfs-log-structured-file-system -a /docs/notes.txt -f ~/notes.txt", "Add notes.txt");
    printf("  %-50s %s\n", "./exfs-log-structured-file-system -r /docs/notes.txt", "Remove notes.txt");
    printf("  %-50s %s\n", "./exfs-log-structured-file-system -e /docs/notes.txt > output.txt", "Extract and save");
    printf("  %-50s %s\n", "./exfs-log-structured-file-system -c", "Run cleaner");
    printf("  %-50s %s\n", "./exfs-log-structured-file-system -D /docs", "Debug directory");
    printf("\n");
}

int main(int argc, char *argv[])
{
    print_banner();

    // Guard clause: Ensure a command was provided
    if (argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)
    {
        print_usage();
        return 0;
    }

    // Handle initialization sequence
    if (strcmp(argv[1], "--init") == 0)
    {
        printf("Initializing fresh file system...\n");
        fs_init();
        printf("File system initialized successfully\n");
        return 0;
    }

    // Guard clause: Verify the file system is mounted before executing standard commands
    if (access("checkpoint.bin", F_OK) != 0)
    {
        fprintf(stderr, COLOR_RED "ERROR: File system not initialized. Run './exfs-log-structured-file-system --init' first\n" COLOR_RESET);
        return 1;
    }

    // Mount the file system
    fs_init();

    // --- Command Routing ---

    if (strcmp(argv[1], "-l") == 0)
    {
        printf("\nFile System Tree:\n");
        printf("-----------------\n");
        fs_list();
        printf("-----------------\n");
    }
    else if (strcmp(argv[1], "-a") == 0)
    {
        if (argc < 5 || strcmp(argv[3], "-f") != 0)
        {
            fprintf(stderr, COLOR_RED "ERROR: Invalid add syntax. Use: ./exfs-log-structured-file-system -a /path -f /host/file\n" COLOR_RESET);
            return 1;
        }

        char *fs_path = argv[2]; 
        char *host_path = argv[4]; 

        // Verify the source file exists on the host machine before attempting to read it
        if (access(host_path, F_OK) != 0)
        {
            fprintf(stderr, COLOR_RED "ERROR: Host file '%s' does not exist\n" COLOR_RESET, host_path);
            return 1;
        }

        printf("Adding file...\n");
        printf("  FS path: %s\n", fs_path);
        printf("  Host: %s\n", host_path);
        fs_add(fs_path, host_path);
        printf(COLOR_GREEN "File added successfully\n" COLOR_RESET);
    }
    else if (strcmp(argv[1], "-r") == 0)
    {
        if (argc < 3)
        {
            fprintf(stderr, COLOR_RED "ERROR: Invalid remove syntax. Use: ./exfs-log-structured-file-system -r /path\n" COLOR_RESET);
            return 1;
        }

        char *fs_path = argv[2];
        printf("Removing: %s\n", fs_path);
        fs_remove(fs_path);
        printf(COLOR_GREEN "Remove complete\n" COLOR_RESET);
    }
    else if (strcmp(argv[1], "-e") == 0)
    {
        if (argc < 3)
        {
            fprintf(stderr, COLOR_RED "ERROR: Invalid extract syntax. Use: ./exfs-log-structured-file-system -e /path\n" COLOR_RESET);
            return 1;
        }

        char *fs_path = argv[2];
        fs_extract(fs_path);
    }
    else if (strcmp(argv[1], "-c") == 0)
    {
        fs_cleaner();
    }
    else if (strcmp(argv[1], "-D") == 0)
    {
        if (argc < 3)
        {
            fprintf(stderr, COLOR_RED "ERROR: Invalid debug syntax. Use: ./exfs-log-structured-file-system -D /path\n" COLOR_RESET);
            return 1;
        }

        char *fs_path = argv[2];
        fs_debug(fs_path);
    }
    else
    {
        fprintf(stderr, COLOR_RED "ERROR: Unknown option '%s'\n" COLOR_RESET, argv[1]);
        print_usage();
        return 1;
    }

    return 0;
}