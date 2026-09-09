CC = gcc
CFLAGS = -Wall -Wextra -std=c11

TARGET = app
OBJS = main.o account.o logger.o

STUDENT_NAME = DoXuanQuy
MSSV = 20235818
ZIP_NAME = $(STUDENT_NAME)_$(MSSV)_HW1.zip

FILES_TO_PACK = main.c account.c account.h logger.c logger.h account.txt Makefile

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

main.o: main.c account.h logger.h
	$(CC) $(CFLAGS) -c main.c

account.o: account.c account.h
	$(CC) $(CFLAGS) -c account.c

logger.o: logger.c logger.h
	$(CC) $(CFLAGS) -c logger.c

package: clean
	zip -r $(ZIP_NAME) $(FILES_TO_PACK)

clean:
	rm -f $(TARGET) $(OBJS) log_*.txt $(ZIP_NAME)

.PHONY: all clean package
