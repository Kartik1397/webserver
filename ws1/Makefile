CC = gcc
CFLAGS = -g -pthread -Werror -Wall -Wextra
BINS = ws
OBJS = main.o queue.o

all: $(BINS)

ws: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $^