# ==============================================================================
# Makefile - Network Programming (IT4062) - Week 5 / Week 6
# Targets: bai1 (server, client), bai2 (server, client)
# ==============================================================================

# Subdirectories for exercises
SUBDIRS = bai1 bai2

# Student submission information
STUDENT_NAME = DoXuanQuy
MSSV = 20235818
ZIP_NAME = $(STUDENT_NAME)_$(MSSV)_HW5.zip

# Files included in the submission archive (source code + Makefiles + initial account.txt only, no .md, no binaries, no .o)
FILES_TO_PACK = Makefile \
                bai1/Makefile bai1/server.c bai1/client.c bai1/account.txt \
                bai2/Makefile bai2/protocol.h bai2/server.c bai2/client.c

# ------------------------------------------------------------------------------
# Build Rules
# ------------------------------------------------------------------------------

# Default target: compile both exercises
all:
	@for dir in $(SUBDIRS); do \
		echo "==> Building $$dir"; \
		$(MAKE) -C $$dir; \
	done

# Create zip archive for submission (DoXuanQuy_20235818_HW5.zip)
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
	$(MAKE) -C bai2 distclean

.PHONY: all clean distclean package zip $(SUBDIRS)
