# ==============================================================================
# Makefile - Network Programming (IT4062) - Week 5/6 - Problem 2
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

all: $(SERVER) $(CLIENT)

$(SERVER): $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $(SERVER) $(SERVER_OBJS)

$(CLIENT): $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $(CLIENT) $(CLIENT_OBJS)

%.o: %.c protocol.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(SERVER) $(CLIENT) $(SERVER_OBJS) $(CLIENT_OBJS) *.o *.tmp downloaded_*

distclean: clean
	rm -rf $(STORAGE_DIR)

.PHONY: all clean distclean
