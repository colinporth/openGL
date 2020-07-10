CC = gcc
CFLAGS += -I/usr/include/GL \
	  -D_GNU_SOURCE \
	  -DPTHREADS \
	  -Wall \
	  -g \
	  -O2

CXX = g++
CXXFLAGS += -std=c++17 \
	    -I/usr/include/GL \
	    -D_GNU_SOURCE \
	    -DPTHREADS \
	    -Wall \
	    -g \
	    -O2

LDFLAGS = -lm -ldl -lGL -lGLEW -lGLU -lX11 -lXext

gears:  gears.o
	$(CXX) -o $@ $^ $(LDFLAGS)

main.o: main.cpp
	$(CXX) -c -o $@ $< $(CXXFLAGS) -MT $< -MD -MP -MF gears.Tpo

.PHONY: clean rebuild

all: gears Makefile

clean:
	rm -f *.o *.~* %*~ gears

rebuild:
	make clean && make -j 4
