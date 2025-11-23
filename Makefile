CFLAGS=-Wno-write-strings -Wno-pointer-arith
LIBS=-lpthread -lgpiod
CC=gcc

all: main

clean:
	rm -fv *.o
	rm -fv main

main: $(OBJS)
	$(CC) $(LIBS) $(CFLAGS) main.c $(OBJS) -o main
