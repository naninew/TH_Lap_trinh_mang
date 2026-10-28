# ==============================================================================
# Makefile - Network Programming (IT4062) - Week 6 - Problem 2
# Targets: server, client
# Student: Do Xuan Quy (MSSV: 20235818)
# ==============================================================================

CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -pthread

SERVER = server
CLIENT = client
SERVER_SRC = server.c
CLIENT_SRC = client.c
SERVER_OBJS = $(SERVER_SRC:.c=.o)
CLIENT_OBJS = $(CLIENT_SRC:.c=.o)

all: $(SERVER) $(CLIENT)

$(SERVER): $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $(SERVER) $(SERVER_OBJS)

$(CLIENT): $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $(CLIENT) $(CLIENT_OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(SERVER) $(CLIENT) $(SERVER_OBJS) $(CLIENT_OBJS) *.o *.tmp

distclean: clean

.PHONY: all clean distclean
