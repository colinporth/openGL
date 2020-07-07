CC = gcc

CFLAGS = -I/usr/include/GL \
	 -D_GNU_SOURCE -DPTHREADS \
	 -Wall -Wpointer-arith -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -Wnested-externs \
	 -Wbad-function-cast -Wold-style-definition -Wdeclaration-after-statement \
	 -fno-strict-aliasing \
	 -O2

LFLAGS = -lGL -lGLEW -lGLU -lGL -lm -lX11 -lXext

glxgears: simple.o
	$(CC) -o $@ $^ $(CFLAGS) $(LFLAGS)

glxgears.o: simple.c
	$(CC) -c -o $@ $< $(CFLAGS) -MT $< -MD -MP -MF glxgears.Tpo

clean:
	rm *.o
	rm simple
