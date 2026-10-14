# ==============================================================================
# Makefile - Network Programming (IT4062) - Week 4 - Problem 2
# Targets: server, client
# ==============================================================================

# Compiler & flags
CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE

# Target executables and source files
SERVER = server
CLIENT = client
SERVER_SRC = server.c
CLIENT_SRC = client.c
SERVER_OBJS = $(SERVER_SRC:.c=.o)
CLIENT_OBJS = $(CLIENT_SRC:.c=.o)

# Student submission information
STUDENT_NAME = DoXuanQuy
MSSV = 20235818
ZIP_NAME = $(STUDENT_NAME)_$(MSSV)_HW4_Bai2.zip

# Files included in the submission archive (source code + Makefile only)
FILES_TO_PACK = server.c client.c Makefile

# ------------------------------------------------------------------------------
# Build Rules
# ------------------------------------------------------------------------------

# Default target: compile both server and client
all: $(SERVER) $(CLIENT)

# Link object files to create the server executable
$(SERVER): $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $(SERVER) $(SERVER_OBJS)

# Link object files to create the client executable
$(CLIENT): $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $(CLIENT) $(CLIENT_OBJS)

# Compile C source files into object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Create zip archive for submission (HotenSV_MSSV_HW4_Bai2.zip)
package: clean
	zip -r $(ZIP_NAME) $(FILES_TO_PACK)

# Alias for package
zip: package

# Remove build artifacts and submission archives
clean:
	rm -f $(SERVER) $(CLIENT) $(SERVER_OBJS) $(CLIENT_OBJS) *.o $(ZIP_NAME) files.zip

# Also delete the directory where the server stores uploaded files
distclean: clean
	rm -rf storage

.PHONY: all clean distclean package zip
