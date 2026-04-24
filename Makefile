CC = gcc
CFLAGS = -I./src
TARGET = exfs-log-structured-file-system

SRC = src/main.c src/fs/fs.c src/inode/inode.c src/imap/imap.c src/utils/utils.c

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

run:
	./${TARGET} --init

clean:
	rm -f $(TARGET)