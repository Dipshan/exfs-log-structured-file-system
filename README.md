# ExFS-Log-Structured File System

A log-structured file system (LFS) implementation in C that appends all updates sequentially to a log.

---

## Authors

- Anuska Bhattarai (800832698) - anuskbh@siue.edu
- Deepshan Adhikari (800846035) - deepadh@siue.edu
- Sumit Shrestha (800835513) - sumishr@siue.edu

---

## Course

**CS514 - Operating Systems**
Instructor: Dr. Igor Crk
Southern Illinois University Edwardsville
Department of Computer Science

---

## Features

- 1MB fixed-size segments
- 4KB blocks and inodes
- 10 direct + single/double/triple indirect block pointers
- Inode map (imap) stored in segments
- Checkpoint region for fast mounting
- Cleaner for garbage collection

---

## Compiling and Configuration

To compile the file system, navigate to the project directory and run:

```bash
make
```

This will generate the executable `exfs-log-structured-file-system` using the provided `Makefile`.

To clean the build files:

```bash
make clean
```

### Initialization

Before using the file system for the first time, initialize it:

```bash
./exfs-log-structured-file-system --init
```

This creates the `checkpoint.bin` and the first segment block.

---

## Testing

To verify file integrity:

```bash
# 1. Add a test file
./exfs-log-structured-file-system -a /testdir/test.txt -f /path/to/local/test.txt

# 2. Extract the file
./exfs-log-structured-file-system -e /testdir/test.txt > extracted.txt

# 3. Compare files
diff /path/to/local/test.txt extracted.txt
```

If `diff` returns no output, the extracted file matches the original.

The codebase has also been tested on the `os.cs.siue.edu` server.

---

## What Works and What Doesn't

### What Works

- **Self-Contained Storage:** Files are broken into blocks and stored within 1MB segment files.
- **Directory and File Creation:** Supports directories and files with automatic parent resolution.
- **Indirect Pointers:** Supports single, double, and triple indirect blocks.
- **Extraction and Removal:** Files can be extracted to `stdout`; recursive directory deletion works.
- **Garbage Collection:** Cleaner migrates live data and removes obsolete segments.

### Known Issues

- _(Update this section if bugs or edge cases are found during testing.)_

---

## Project Folder Structure

```bash
exfs-log-structured-file-system/
│
├── .gitignore
├── Makefile
├── README.md
│
└── src/
    ├── common.h
    │
    ├── fs/
    │   ├── fs.h
    │   └── fs.c
    │
    ├── inode/
    │   ├── inode.h
    │   └── inode.c
    │
    ├── imap/
    │   ├── imap.h
    │   └── imap.c
    │
    ├── utils/
    │   ├── utils.h
    │   └── utils.c
    │
    └── main.c
```

```

```
