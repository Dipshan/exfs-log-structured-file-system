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
                    ┌─────────────────────────────┐
                    │        User Interface       │
                    │  -a  -e  -l  -r  -c  -D     │
                    └─────────────┬───────────────┘
                                  │
                                  ▼
                    ┌─────────────────────────────┐
                    │      File System Core       │
                    │ (fs.c, inode.c, imap.c)     │
                    └─────────────┬───────────────┘
                                  │
                                  |
                                  ▼
       ┌──────────────────────────-──────────────────────────┐
       │                                                     │
       ▼                                                     ▼
┌───────────────────────────┐                  ┌─────────────────────────────┐
│     Checkpoint Region     │                  │        Segment Log          │
│       checkpoint.bin      │                  │   segment0.bin, segment1…   │
│                           │                  │                             │
│  - Write Head             │                  │  - Data Blocks (4KB)        │
│  - Imap Location          │                  │  - Inodes (4KB)             │
│  - Next Inode Number      │                  │  - Directory Entries        │
└─────────────┬─────────────┘                  │  - Imap Chunks              │
             │                                └─────────────┬───────────────┘
             ▼                                              │
                                                            |
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

# How to Compile and Initialize

```
make
./exfs-log-structured-file-system --init
```

# Commands

## List the file system

```
./exfs-log-structured-file-system -l
```

## Add a file

```
./exfs-log-structured-file-system -a /docs -f test.txt
```

This stores the host file as:

/docs/test.txt

## Extract a file

```
./exfs-log-structured-file-system -e /docs/test.txt > output.txt
```

## Remove a file or directory

```
./exfs-log-structured-file-system -r /docs/test.txt
```

## Run cleaner

```
./exfs-log-structured-file-system -c
```

## Debug mode

```
./exfs-log-structured-file-system -D /docs
```

# Testing

## Text File Test

```
echo "hello" > test.txt
./exfs-log-structured-file-system -a /docs -f test.txt
./exfs-log-structured-file-system -e /docs/test.txt > out.txt
diff test.txt out.txt
```

Expected: no output

## Binary File Test

```
head -c 10000 /dev/urandom > binary.bin
./exfs-log-structured-file-system -a /bin -f binary.bin
./exfs-log-structured-file-system -e /bin/binary.bin > binary_out.bin
cmp binary.bin binary_out.bin
```

Expected: no output

## Large File Test

```
head -c 50000 /dev/urandom > large.bin
./exfs-log-structured-file-system -a /large -f large.bin
./exfs-log-structured-file-system -e /large/large.bin > large_out.bin
cmp large.bin large_out.bin
```

## Segment Growth Test

```
head -c 1200000 /dev/urandom > segtest.bin
./exfs-log-structured-file-system -a /seg -f segtest.bin
ls -lh segments
./exfs-log-structured-file-system -e /seg/segtest.bin > persisted.bin
cmp segtest.bin persisted.bin
```

# Notes

The -a command treats the given path as a destination directory, and the file name is taken from the host file.

Example:

```
./exfs-log-structured-file-system -a /docs -f test.txt
```

creates:

/docs/test.txt

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

- The `-a` path is treated as a directory, not a full file path
- Paths like `/file.txt` may create `/file.txt/file.txt`
- Duplicate directories may appear if paths are reused
- Cleaner works for basic cases but not heavily tested
- Some output is printed to stdout, which can affect extraction
