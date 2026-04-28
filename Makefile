# Compiler and flags
CC = gcc
CFLAGS = -I./src -Wall

# Target executable name
TARGET = exfs-log-structured-file-system

# Source files
SRCS = src/main.c \
       src/fs/fs.c \
       src/inode/inode.c \
       src/imap/imap.c \
       src/utils/utils.c

# Default target
all: $(TARGET)

# Compile directly from source to executable
$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)

# Initialize file system
init: $(TARGET)
	./$(TARGET) --init

# Remove executable
clean:
	rm -f $(TARGET)

# Remove segments and checkpoint
clean-fs:
	rm -rf segments checkpoint.bin

# Remove everything
clean-all: clean clean-fs

# Complete reset
reset: clean-all all init

# Help menu
help:
	@echo ""
	@echo "ExFS-Log Makefile Commands:"
	@echo "-----------------------------"
	@echo "make               - Compile the file system"
	@echo "make init          - Initialize fresh file system"
	@echo "make clean         - Remove executable"
	@echo "make clean-fs      - Remove segments and checkpoint"
	@echo "make clean-all     - Remove everything"
	@echo "make reset         - Complete reset"
	@echo "make help          - Show this help menu"
	@echo ""

.PHONY: all init clean clean-fs clean-all reset help