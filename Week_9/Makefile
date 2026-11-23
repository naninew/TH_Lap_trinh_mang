# ==============================================================================
# Makefile - Network Programming (IT4062) - Week 7 (Problem 2) / Week 9
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

STUDENT_NAME = DoXuanQuy
MSSV = 20235818
ZIP_NAME = $(STUDENT_NAME)_$(MSSV)_HW7_Bai2.zip
ZIP_NAME_ALT = $(STUDENT_NAME)_$(MSSV)_HW8.zip

FILES_TO_PACK = Makefile server.c client.c account.txt

all: $(SERVER) $(CLIENT)

$(SERVER): $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $(SERVER) $(SERVER_OBJS)

$(CLIENT): $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $(CLIENT) $(CLIENT_OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

package: clean
	zip -r $(ZIP_NAME) $(FILES_TO_PACK)

zip: package

package8: clean
	zip -r $(ZIP_NAME_ALT) $(FILES_TO_PACK)

clean:
	rm -f $(SERVER) $(CLIENT) $(SERVER_OBJS) $(CLIENT_OBJS) *.o *.tmp $(ZIP_NAME) $(ZIP_NAME_ALT)

distclean: clean

.PHONY: all clean distclean package zip package8
