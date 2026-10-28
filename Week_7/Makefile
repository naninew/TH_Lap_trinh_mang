# ==============================================================================
# Makefile - Network Programming (IT4062) - Week 6
# Targets: bai1 (server, client), bai2 (server, client)
# Student: Do Xuan Quy (MSSV: 20235818)
# ==============================================================================

# Subdirectories for exercises
SUBDIRS = bai1 bai2

# Student submission information
STUDENT_NAME = DoXuanQuy
MSSV = 20235818
ZIP_NAME = $(STUDENT_NAME)_$(MSSV)_HW6.zip

# Files included in the submission archive (source code + Makefiles + initial account.txt only, no .md, no binaries, no .o)
FILES_TO_PACK = Makefile \
                bai1/Makefile bai1/protocol.h bai1/server.c bai1/client.c \
                bai2/Makefile bai2/server.c bai2/client.c bai2/account.txt

# ------------------------------------------------------------------------------
# Build Rules
# ------------------------------------------------------------------------------

# Default target: compile both exercises
all:
	@for dir in $(SUBDIRS); do \
		echo "==> Building $$dir"; \
		$(MAKE) -C $$dir; \
	done

# Create zip archive for submission (DoXuanQuy_20235818_HW6.zip)
package: clean
	zip -r $(ZIP_NAME) $(FILES_TO_PACK)

# Alias for package
zip: package

# Remove build artifacts and submission archives
clean:
	@for dir in $(SUBDIRS); do \
		echo "==> Cleaning $$dir"; \
		$(MAKE) -C $$dir clean; \
	done
	rm -f $(ZIP_NAME)

# Full clean including storage folder
distclean: clean
	$(MAKE) -C bai1 distclean

.PHONY: all clean distclean package zip $(SUBDIRS)
