CC=gcc
CFLAGS=-Wall -Wextra -Werror -g

TARGETS=minls minget

FSOBJ=ffs.o

all: $(TARGETS)

minls: minls.o $(FSOBJ)
	$(CC) $(CFLAGS) -o $@ $^

minget: minget.o $(FSOBJ)
	$(CC) $(CFLAGS) -o $@ $^

ffs.o: ffs.c ffs.h
	$(CC) $(CFLAGS) -c ffs.c

minls.o: minls.c ffs.h
	$(CC) $(CFLAGS) -c minls.c

minget.o: minget.c ffs.h
	$(CC) $(CFLAGS) -c minget.c

ffs: ffs.c ffs.h
	$(CC) $(CFLAGS) -DDEBUG_MAIN ffs.c -o ffs

run-ffs: ffs
	./ffs $(ARGS)

clean:
	rm -f *.o $(TARGETS) ffs

rebuild: clean all