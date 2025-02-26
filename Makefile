CC = gcc
CFLAGS = -Wall -Wextra -std=c99
LDFLAGS =
SOURCES = main.c disco_virtual.c memoria.c
OBJECTS = $(SOURCES:.c=.o)
EXECUTABLE = mini_sistema

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(EXECUTABLE)

.PHONY: all clean