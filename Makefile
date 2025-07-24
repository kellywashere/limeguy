# source: https://stackoverflow.com/questions/7004702/how-can-i-create-a-makefile-for-c-projects-with-src-obj-and-bin-subdirectories
# https://stackoverflow.com/questions/12605051/how-to-check-if-a-directory-doesnt-exist-in-make-and-create-it
# added run task

CC       = gcc
# compiling flags here
# Valgrind//debug:
CFLAGS   := -Wall -Werror -Wextra -Wshadow -I. -O2 -std=gnu99
# CFLAGS   := -Wall -Werror -Wextra -Wshadow -I. -std=gnu99 -g

LINKER   = gcc
# linking flags here
LFLAGS   := -I. -lm -lraylib

# change these to proper directories where each file should be
SRCDIR   = src
OBJDIR   = obj
BINDIR   = bin

# output executable
TARGET   = limeguy

SOURCES  := $(wildcard $(SRCDIR)/*.c)
INCLUDES := $(wildcard $(SRCDIR)/*.h)
OBJECTS  := $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
rm       = rm -f

$(BINDIR)/$(TARGET): $(OBJECTS) | $(BINDIR)
	$(LINKER) $(OBJECTS) $(LFLAGS) -o $@
	@echo "Linking complete!"

$(BINDIR):
	mkdir -p $@

$(OBJECTS): $(OBJDIR)/%.o : $(SRCDIR)/%.c $(INCLUDES) | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@
	@echo "Compiled "$<" successfully!"

$(OBJDIR):
	mkdir -p $@

.PHONY: run
run: $(BINDIR)/$(TARGET)
	@./$(BINDIR)/$(TARGET)

.PHONY: memcheck
memcheck:
	valgrind --leak-check=yes ./$(BINDIR)/$(TARGET)

.PHONY: memcheckfull
memcheckfull:
	valgrind --leak-check=full --track-origins=yes --show-reachable=yes ./$(BINDIR)/$(TARGET)

.PHONY: clean
clean:
	@$(rm) $(OBJECTS)
	@echo "Cleanup complete!"

.PHONY: remove
remove: clean
	@$(rm) $(BINDIR)/$(TARGET)
	@echo "Executable removed!"


