CC=gcc
DEPS = main.c
OBJ = main.o

ws: $(OBJ)
	$(CC) -o $@ $^

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $<

clean: a.out
	rm a.out