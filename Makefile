# ciscoterm Makefile
# Targets: all, clean, install, debug

CC      := gcc
TARGET  := ciscoterm

SRCS    := main.c ui.c modes.c serial.c parser.c
OBJS    := $(SRCS:.c=.o)
HDRS    := ui.h modes.h serial.h parser.h

# Base flags
CFLAGS  := -Wall -Wextra -Wpedantic -std=c11
LDFLAGS := -lncurses

# Release build
CFLAGS  += -O2

# Debug build: make debug
debug: CFLAGS := -Wall -Wextra -std=c11 -g -fsanitize=address -DDEBUG
debug: LDFLAGS := -lncurses -fsanitize=address
debug: $(TARGET)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Built $(TARGET)"

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/$(TARGET)
	@echo "Installed to /usr/local/bin/$(TARGET)"

# Install build dependencies on Raspberry Pi / Ubuntu
deps:
	sudo apt-get update
	sudo apt-get install -y libncurses5-dev libncursesw5-dev gcc make

.PHONY: all clean install deps debug
