CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -Wpedantic -O2

.PHONY: all clean
all: locked_editor

locked_editor: locked_editor.c
	$(CC) $(CFLAGS) -o locked_editor locked_editor.c

clean:
	rm -f locked_editor
