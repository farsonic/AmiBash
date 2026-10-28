# Compare src/regex.c against glibc regcomp/regexec.
CC = gcc
CFLAGS = -std=c99 -O2 -g -Wall -Wextra -Wno-unused-parameter

test_regex: test_regex.o regex.o stubs.o
	$(CC) -o $@ test_regex.o regex.o stubs.o

test_regex.o: test_regex.c
	$(CC) $(CFLAGS) -D_GNU_SOURCE -c test_regex.c

regex.o: ../../src/regex.c ../../src/sh.h
	$(CC) $(CFLAGS) -I../../src -c ../../src/regex.c

check: test_regex
	./test_regex

clean:
	rm -f test_regex *.o
