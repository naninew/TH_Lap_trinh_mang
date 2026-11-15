# ==============================================================================
# Makefile - Network Programming (IT4062) - Week 7 / Week 8
# Targets: server, client
# Student: Do Xuan Quy (MSSV: 20235818)
# ==============================================================================

CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE

SERVER = server
CLIENT = client
SERVER_SRC = server.c
CLIENT_SRC = client.c
SERVER_OBJS = $(SERVER_SRC:.c=.o)
CLIENT_OBJS = $(CLIENT_SRC:.c=.o)
STORAGE_DIR = storage

# Student submission information
STUDENT_NAME = DoXuanQuy
MSSV = 20235818
ZIP_NAME = $(STUDENT_NAME)_$(MSSV)_HW7.zip

# Files included in the submission archive (source code + headers + Makefile only, no .md, no binaries, no .o)
FILES_TO_PACK = Makefile protocol.h server.c client.c

# ------------------------------------------------------------------------------
# Build Rules
# ------------------------------------------------------------------------------

# Default target: compile both server and client
all: $(SERVER) $(CLIENT)

$(SERVER): $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $(SERVER) $(SERVER_OBJS)

$(CLIENT): $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $(CLIENT) $(CLIENT_OBJS)

%.o: %.c protocol.h
	$(CC) $(CFLAGS) -c $< -o $@

# Create zip archive for submission (DoXuanQuy_20235818_HW7.zip)
package: clean
	zip -r $(ZIP_NAME) $(FILES_TO_PACK)

# Alias for package
zip: package

# Remove build artifacts, temporary test files, and submission archive
clean:
	rm -f $(SERVER) $(CLIENT) $(SERVER_OBJS) $(CLIENT_OBJS) *.o *.tmp cipher*.txt restored*.txt $(ZIP_NAME)

# Full clean including storage folder
distclean: clean
	rm -rf $(STORAGE_DIR)

.PHONY: all clean distclean package zip

