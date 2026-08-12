CC = gcc
CFLAGS = -Wall -O3 -march=native -std=c99 -D_POSIX_C_SOURCE=200809L
TARGET = xxhash
SRCS = src/main.c src/xxhash.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^
	rm -f $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
