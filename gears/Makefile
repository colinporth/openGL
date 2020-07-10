CC = gcc
CFLAGS = -I/usr/include/GL \
	 -D_GNU_SOURCE -DPTHREADS \
	 -Wall \
	 -fno-strict-aliasing \
	 -O2

LFLAGS = -lm -lGL -lGLEW -lGLU -lX11 -lXext

all: main Makefile

main.o: main.c
	$(CC) -c -o $@ $< $(CFLAGS) -MT $< -MD -MP -MF main.Tpo

main:  main.o
	$(CC) -o $@ $^ $(LFLAGS)

.PHONY: clean rebuild

clean:
	rm -f *.o *.~* %*~ main

rebuild:
	make clean && make -j 4
