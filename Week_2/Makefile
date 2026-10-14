# ==============================================================================
# Makefile - Network Programming (IT4062) - Week 2
# Target: resolver
# ==============================================================================

# Compiler & flags
CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g

# Target executable and source files
TARGET = resolver
SRC = resolver.c
OBJS = $(SRC:.c=.o)

# Student submission information
STUDENT_NAME = DoXuanQuy
MSSV = 20235818
ZIP_NAME = $(STUDENT_NAME)_$(MSSV)_HW2.zip

# Files included in the submission archive
FILES_TO_PACK = resolver.c Makefile

# ------------------------------------------------------------------------------
# Build Rules
# ------------------------------------------------------------------------------

# Default target: compile the resolver application
all: $(TARGET)

# Link object files to create the final executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

# Compile C source files into object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Create zip archive for submission (HotenSV_MSSV_HW2.zip)
package: clean
	zip -r $(ZIP_NAME) $(FILES_TO_PACK)

# Alias for package
zip: package

# Remove build artifacts and submission archives
clean:
	rm -f $(TARGET) $(OBJS) *.o $(ZIP_NAME) files.zip

.PHONY: all clean package zip
