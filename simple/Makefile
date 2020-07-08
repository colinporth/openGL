CC = gcc

CFLAGS = -I/usr/include/GL \
	 -D_GNU_SOURCE -DPTHREADS \
	 -Wall -Wpointer-arith -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -Wnested-externs \
	 -Wbad-function-cast -Wold-style-definition -Wdeclaration-after-statement \
	 -fno-strict-aliasing \
	 -O2

LFLAGS = -lGL -lGLEW -lGLU -lGL -lm -lX11 -lXext

simple: simple.o
	$(CC) -o $@ $^ $(CFLAGS) $(LFLAGS)

simple.o: simple.c
	$(CC) -c -o $@ $< $(CFLAGS) -MT $< -MD -MP -MF simple.Tpo

clean:
	rm *.o
	rm simple
