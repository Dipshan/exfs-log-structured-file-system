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

# Object files
OBJS = $(SRCS:.c=.o)

# Default target
all: $(TARGET)

# Link object files into executable
$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET)

# Compile .c files into .o files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Initialize file system
init: $(TARGET)
	./$(TARGET) --init

# Remove executable and object files
clean:
	rm -f $(TARGET) $(OBJS)

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
	@echo "make clean         - Remove executable and object files"
	@echo "make clean-fs      - Remove segments and checkpoint"
	@echo "make clean-all     - Remove everything"
	@echo "make reset         - Complete reset"
	@echo "make help          - Show this help menu"
	@echo ""

.PHONY: all init clean clean-fs clean-all reset help