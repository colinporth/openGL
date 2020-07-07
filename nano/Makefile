CC = gcc
CXX = g++

# -g -O2
CFLAGS   += -Wall -O2
CXXFLAGS += -Wall -O2 -std=c++11

INCLUDES +=

SRC       = mainGL.cpp \
	    demo.cpp \
	    perf.cpp \
	    nanoVg.cpp \
	    ../../shared/glad/glad.c

OBJS     += $(filter %.o,$(SRC:.cpp=.o)) $(filter %.o,$(SRC:.c=.o))

LDFLAGS  += -l GL -l glfw -l dl

all: nano Makefile

%.o: %.c Makefile
	@rm -f $@
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

%.o: %.cpp  Makefile
	@rm -f $@
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

nano: $(OBJS) Makefile
	$(CXX) $(LDFLAGS) -o nano $(OBJS)

.PHONY: clean rebuild

clean:
	rm -f *.o *.~* %*~ nano

rebuild:
	make clean && make