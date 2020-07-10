CC = gcc
CFLAGS = -I/usr/include/GL \
	 -D_GNU_SOURCE -DPTHREADS \
	 -Wall \
	 -fno-strict-aliasing \
	 -O2

LFLAGS  = -lm -lGL -lGLEW -lGLU -lX11 -lXext

all: gears Makefile

gears:  gears.o
	$(CC) -o $@ $^ $(LFLAGS)

main.o: main.c
	$(CC) -c -o $@ $< $(CFLAGS) -MT $< -MD -MP -MF gears.Tpo

.PHONY: clean rebuild

clean:
	rm -f *.o *.~* %*~ gears

rebuild:
	make clean && make -j 4
