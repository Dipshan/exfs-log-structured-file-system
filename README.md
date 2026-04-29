# ExFS-Log: Log-Structured File System

## Authors

- Anuska Bhattarai (800832698) - anuskbh@siue.edu
- Deepshan Adhikari (800846035) - deepadh@siue.edu
- Sumit Shrestha (800835513) - sumishr@siue.edu

## Course

CS514 - Operating Systems
Instructor: Dr. Igor Crk
Department of Computer Science
Southern Illinois University Edwardsville

## Features

- 1MB fixed-size segments for append-only log storage
- 4KB blocks and inodes aligned with file system constraints
- 10 direct + single/double/triple indirect block pointers
- Inode map (imap) stored in segments (not just memory)
- Checkpoint region for fast mounting and crash recovery
- Cleaner for garbage collection and space reclamation
- Full binary file support (text and binary files)
- Recursive directory creation and deletion

## Requirements

- GCC compiler
- Make
- Standard C libraries
- macOS or Linux

# System Architecture

```
                    ┌───────────────────────────┐
                    │        User Interface     │
                    │  -a  -e  -l  -r  -c  -D   │
                    └─────────────┬─────────────┘
                                  │
                                  ▼
                    ┌──────────────────────────┐
                    │      File System Core    │
                    │ (fs.c, inode.c, imap.c)  │
                    └─────────────┬────────────┘
                                  │
                                  |
                                  ▼
       ┌──────────────────────────-──────────────────────────┐
       │                                                     │
       ▼                                                     ▼
┌──────────────────────────┐                   ┌────────────────────────────┐
│     Checkpoint Region    │                   │        Segment Log         │
│       checkpoint.bin     │                   │   segment0.bin.            │
│                          │                   │                            │
│  - Write Head            │                   │  - Data Blocks (4KB)       │
│  - Imap Location         │                   │  - Inodes (4KB)            │
│  - Next Inode Number     │                   │  - Directory Entries       │
└────────────┬─────────────┘                   │  - Imap Chunks             │
             │                                 └────────────┬───────────────┘
             ▼                                              │
    ┌─────────────────────────────┐                         │
    │           IMAP              │◄────────────────────────┘
    │  (Inode → Segment, Offset)  │
    └─────────────────────────────┘
```

## Description

The system follows a **log-structured design**, where all updates are appended sequentially to segment files.

- The **User Interface** provides commands for adding, extracting, listing, removing, and cleaning files.
- The **File System Core** handles all logic related to inode management, directory traversal, and log appends.
- The **Checkpoint Region** maintains global state, including the current write position and imap location.
- The **Segment Log** stores all data structures (inodes, file data, directory entries) in append-only segments.
- The **IMAP (Inode Map)** tracks the latest location of each inode, enabling efficient lookup and recovery.

### Data Flow

- **Add (-a):** File → Data Blocks → Inode → IMAP update → Checkpoint update
- **Extract (-e):** Checkpoint → IMAP → Inode → Data Blocks → Output
- **Cleaner (-c):** Identifies live data → rewrites → removes obsolete segments

This design ensures **sequential disk writes, efficient recovery, and consistency without in-place updates**.

# Project Folder Structure
```
exfs-log-structured-file-system/
      ├── segments/   
      ├── src/
      │   ├── common.h       # Global constants (BLOCK_SIZE, SEGMENT_SIZE, location struct)
      │   ├── main.c         # Entry point - CLI parser and command routerof the program
      │   ├── fs/
      │   │     ├── fs.h     # FS API declarations and checkpoint struct
      │   │     └── fs.c     # Core FS: segments, checkpoint, cleaner, add, extract, remove
      │   │
      │   ├── inode/
      │   │     ├── inode.h  # Inode and directory entry structs
      │   │     └── inode.c  # Inode management and indirect block traversal
      │   │
      │   ├── imap/
      │   │     ├── imap.h   # Imap chunk structure (512 mappings per 4KB)
      │   │     └── imap.c   # In-memory cache + segment flushing for imap
      │   │
      │   └── utils/
      │         ├── utils.h  # Safe I/O and path resolution helpers
      │         └── utils.c  # safe_read/write, split_path, find_inode, create_parent_dirs
      │
      ├── Makefile           # Build automation with compile, clean, init commands
      └── README.md          # Project documentation and testing guide
  
```

# List of Commands

## Makefile Commands

| Commands       | What it does                        |
| -------------- | ----------------------------------- |
| make           | Compiles the file system            |
| make init      | Initializes a fresh file system     |
| make clean     | Deletes executable                  |
| make clean-fs  | Deletes segments and checkpoint.bin |
| make clean-all | Deletes everything                  |
| make help      | Shows all commands                  |

## Test Commands

| Commands                                                         | What it does                            |
| ---------------------------------------------------------------- | --------------------------------------- |
| ./exfs-log-structured-file-system --init                         | Initialize file system                  |
| ./exfs-log-structured-file-system -l                             | List all files                          |
| ./exfs-log-structured-file-system -a /docs -f test.txt           | Stores the host file as: /docs/test.txt |
| ./exfs-log-structured-file-system -e /docs/test.txt > output.txt | Extract a file                          |
| ./exfs-log-structured-file-system -r /docs/test.txt              | Remove a file                           |
| ./exfs-log-structured-file-system -r /docs                       | Remove a directory                      |
| ./exfs-log-structured-file-system -D /test.txt                   | Debug an inode                          |
| ./exfs-log-structured-file-system -c                             | Run cleaner                             |
| ./exfs-log-structured-file-system -h                             | Show help                               |

# Testing

## Text File Test

```
echo "hello" > test.txt // file creation
./exfs-log-structured-file-system -a /docs -f test.txt
./exfs-log-structured-file-system -e /docs/test.txt > out.txt
diff test.txt out.txt && echo 'No difference'

Expected output: No difference
```

## Binary File Test

```
head -c 10000 /dev/urandom > binary.bin
./exfs-log-structured-file-system -a /bin -f binary.bin
./exfs-log-structured-file-system -e /bin/binary.bin > binary_out.bin
cmp binary.bin binary_out.bin && echo 'No difference'

Expected output: No difference
```

## Large File Test

### Single Indirect Block Test (1MB)

```
dd if=/dev/urandom of=1MB.bin bs=1M count=1 2>/dev/null
./exfs-log-structured-file-system -a /large/1MB.bin -f 1MB.bin
./exfs-log-structured-file-system -D /large/1MB.bin | grep -A2 Indirect

Expected output:

Indirect:
  Single: yes
  Double: no
```

### Double Indirect Block Test (5MB)

```
dd if=/dev/urandom of=5MB.bin bs=1M count=5 2>/dev/null
./exfs-log-structured-file-system -a /large/5MB.bin -f 5MB.bin
./exfs-log-structured-file-system -D /large/5MB.bin | grep -A2 Indirect

Expected output:

Indirect:
  Single: yes
  Double: yes
```

### Triple Indirect Block Test (1.1GB)

```
dd if=/dev/urandom of=1GB.bin bs=1M count=1100 2>/dev/null
./exfs-log-structured-file-system -a /large/1GB.bin -f 1GB.bin
./exfs-log-structured-file-system -D /large/1GB.bin | grep -A2 Indirect

Expected output:

Indirect:
  Single: yes
  Double: yes
  Triple: yes
```

### Extract and Verify Large Files

```
./exfs-log-structured-file-system -e /large/1MB.bin > 1MB_out.bin
cmp 1MB.bin 1MB_out.bin && echo 'No difference'

./exfs-log-structured-file-system -e /large/5MB.bin > 5MB_out.bin
cmp 5MB.bin 5MB_out.bin && echo 'No difference'

./exfs-log-structured-file-system -e /large/1GB.bin > 1GB_out.bin
cmp 1GB.bin 1GB_out.bin && echo 'No difference'
```

## Segment Growth Test

```
head -c 1200000 /dev/urandom > segtest.bin
./exfs-log-structured-file-system -a /seg -f segtest.bin
ls -lh segments
./exfs-log-structured-file-system -e /seg/segtest.bin > persisted.bin
cmp segtest.bin persisted.bin
```

## Segment clean test
```
# Create 3MB file (spans multiple segments)
dd if=/dev/urandom of=temp.bin bs=1M count=3 2>/dev/null

# Add to file system
./exfs-log-structured-file-system -a /temp/temp.bin -f temp.bin

# Check segments before deletion
./exfs-log-structured-file-system -r /temp

# Run cleaner to reclaim space
./exfs-log-structured-file-system -c

# Verify old segments are deleted
ls -lh segments/

Expected Workflow:

Before removal:
segments/
  ├── segment0.bin
  ├── segment1.bin
  └── segment2.bin

After removal:
segments/
  └── segment3.bin   (or newer, old segments deleted)

Expected Cleaner Output:

----- CLEANER -----
Current active segment: 2
Current write offset: 3145728

Live inodes: 2
Live blocks: 0

----- CLEANER COMPLETE -----
```

# Notes

The -a command treats the given path as a destination directory, and the file name is taken from the host file.

Example:

```
./exfs-log-structured-file-system -a /docs -f test.txt

creates: /docs/test.txt

System messages are printed to stderr to avoid interfering with file extraction via stdout redirection.
```

# What Works and Known Issues

## What Works

- Files can be added using `-a`, and directories are created automatically
- Listing with `-l` shows the correct directory structure
- File extraction with `-e` works for both text and binary files
- Outputs match exactly (verified using `diff` and `cmp`)
- Large files are handled correctly using indirect blocks
- Files larger than 1MB are stored across multiple segments
- Data persists across runs using checkpoint and imap

## Known Issues

- Cleaner works for standard cases but has not been extensively stress-tested under heavy workloads
