---
title: "ExFS-Log: Log-Structured File System"
author: "Anushka Bhattarai, Deepshan Adhikari, Sumit Shrestha"
date: "`r Sys.Date()`"
output:
  html_document: default
  pdf_document: default
---

# Overview

ExFS-Log is a log-structured file system implemented in C for CS514 Operating Systems. Instead of overwriting data in place, the file system appends updates to segment files. It stores file data, inodes, directory entries, and imap updates inside the log.

The system uses:
- 1MB segment files stored in `segments/`
- 4096-byte blocks
- Inodes with direct and indirect block pointers
- checkpoint.bin to track the current write head and imap location
- An inode map to locate the newest version of each inode

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
                     │ (fs.c, inode.c, imap.c)    │
                     └─────────────┬───────────────┘
                                   │
        ┌──────────────────────────┴──────────────────────────┐
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
     ┌─────────────────────────────┐                         │
     │           IMAP              │◄────────────────────────┘
     │  (Inode → Segment, Offset) │
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

```{bash, eval=FALSE}
make
./exfs-log-structured-file-system --init
```

# Commands

## List the file system

```{bash, eval=FALSE}
./exfs-log-structured-file-system -l
```

## Add a file

```{bash, eval=FALSE}
./exfs-log-structured-file-system -a /docs -f test.txt
```

This stores the host file as:

/docs/test.txt

## Extract a file

```{bash, eval=FALSE}
./exfs-log-structured-file-system -e /docs/test.txt > output.txt
```

## Remove a file or directory

```{bash, eval=FALSE}
./exfs-log-structured-file-system -r /docs/test.txt
```

## Run cleaner

```{bash, eval=FALSE}
./exfs-log-structured-file-system -c
```

## Debug mode

```{bash, eval=FALSE}
./exfs-log-structured-file-system -D /docs
```

# Testing

## Text File Test

```{bash, eval=FALSE}
echo "hello" > test.txt
./exfs-log-structured-file-system -a /docs -f test.txt
./exfs-log-structured-file-system -e /docs/test.txt > out.txt
diff test.txt out.txt
```

Expected: no output

## Binary File Test

```{bash, eval=FALSE}
head -c 10000 /dev/urandom > binary.bin
./exfs-log-structured-file-system -a /bin -f binary.bin
./exfs-log-structured-file-system -e /bin/binary.bin > binary_out.bin
cmp binary.bin binary_out.bin
```

Expected: no output

## Large File Test

```{bash, eval=FALSE}
head -c 50000 /dev/urandom > large.bin
./exfs-log-structured-file-system -a /large -f large.bin
./exfs-log-structured-file-system -e /large/large.bin > large_out.bin
cmp large.bin large_out.bin
```

## Segment Growth Test

```{bash, eval=FALSE}
head -c 1200000 /dev/urandom > segtest.bin
./exfs-log-structured-file-system -a /seg -f segtest.bin
ls -lh segments
./exfs-log-structured-file-system -e /seg/segtest.bin > persisted.bin
cmp segtest.bin persisted.bin
```

# Notes

The -a command treats the given path as a destination directory, and the file name is taken from the host file.

Example:

```{bash, eval=FALSE}
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

# Conclusion

ExFS-Log implements a working log-structured file system using segment-based storage, checkpoint recovery, inode mapping, and append-only updates. It supports directory creation, file insertion, extraction, removal, binary-safe I/O, and large files spanning multiple segments.