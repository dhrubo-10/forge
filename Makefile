# Required: gcc, zlib (zlib1g-dev on linux)

CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -Wshadow -Wpedantic \
           -O2 -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64
LDFLAGS = -lz

TARGET  = forge
SRCS    = forge.c sha1.c objects.c index.c refs.c remote.c
OBJS    = $(SRCS:.c=.o)

.PHONY: all clean install uninstall debug

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo ""
	@echo "  Built: ./$(TARGET)"
	@echo "  Run:   ./forge help"
	@echo ""

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

debug: CFLAGS += -g3 -DDEBUG -fsanitize=address,undefined
debug: LDFLAGS += -fsanitize=address,undefined
debug: $(TARGET)

forge.o:   forge.c   forge.h sha1.h objects.h index.h refs.h remote.h
sha1.o:    sha1.c    sha1.h
objects.o: objects.c objects.h forge.h sha1.h
index.o:   index.c   index.h  forge.h objects.h
refs.o:    refs.c    refs.h   forge.h
remote.o:  remote.c  remote.h forge.h refs.h

install: $(TARGET)
	cp $(TARGET) $(HOME)/.local/bin/forge
	@echo "Installed to ~/.local/bin/forge"
	@echo "Make sure ~/.local/bin is in your PATH."

uninstall:
	rm -f $(HOME)/.local/bin/forge

clean:
	rm -f $(OBJS) $(TARGET)