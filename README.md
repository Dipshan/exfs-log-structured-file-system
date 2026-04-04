# CIS - Collaborative Interactive Shell
A log-structured file system (LFS) implementation in C that appends all updates sequentially to a log.

## Authors
- Anuska Bhattarai (800832698) - anuskbh@siue.edu
- Deepshan Adhikari (800846035) - deepadh@siue.edu
- Sumit Shrestha (800835513) - sumishr@siue.edu

## Course
CS514 - Operating Systems  
Instructor: Dr. Igor Crk  
Southern Illinois University Edwardsville
Department of Computer Science

## Features

- 1MB fixed-size segments
- 4KB blocks and inodes
- 10 direct + single/double/triple indirect block pointers
- Inode map (imap) stored in segments
- Checkpoint region for fast mounting
- Cleaner for garbage collection

## Project Folder Structure
```bash
exfs-log-structured-file-system/
│
├── .gitignore              # Git ignore rules
├── Makefile                # Build instructions
├── README.md               # Project documentation
│
└── src/
    ├── common.h            # Shared constants and types
    │
    ├── fs/
    │   ├── fs.h            # FS operations declarations
    │   └── fs.c            # FS operations implementation
    │
    ├── inode/
    │   ├── inode.h         # Inode structure declarations
    │   └── inode.c         # Inode + indirect block logic
    │
    ├── imap/
    │   ├── imap.h          # Imap declarations
    │   └── imap.c          # Imap stored in segments
    │
    ├── utils/
    │   ├── utils.h         # Helper declarations
    │   └── utils.c         # Path + helper functions
    │
    └── main.c              # Command line interface
```