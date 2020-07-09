CC = gcc
CFLAGS   += -Wall \
	    -g \
	    -O2 \
	    -D _LARGEFILE64_SOURCE -D _FILE_OFFSET_BITS=64

CXX = g++
CXXFLAGS += -std=c++17 \
	    -Wall\
	    -g \
	    -O2 \
	    -D _LARGEFILE64_SOURCE -D _FILE_OFFSET_BITS=64

INCLUDES +=

SRC       = main.cpp \
	    ../../shared/utils/cLog.cpp \
	    ../../shared/teensyAac/huffman.cpp \
	    ../../shared/teensyAac/cAacDecoder.cpp \
	    ../../shared/teensyAac/sbr.cpp \
	    ../../shared/nanoVg/cGlWindow.cpp \
	    ../../shared/nanoVg/cVg.cpp \
	    ../../shared/glad/glad.c \

OBJS     += $(filter %.o,$(SRC:.cpp=.o)) $(filter %.o,$(SRC:.c=.o))

LDFLAGS  += -l dl -l pthread -l asound -l glfw -l GL

all: radio Makefile

%.o: %.c Makefile
	@rm -f $@
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

%.o: %.cpp  Makefile
	@rm -f $@
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

radio: $(OBJS) Makefile
	$(CXX) $(LDFLAGS) -o radio $(OBJS)

.PHONY: clean rebuild

clean:
	rm -f *.o *.~* %*~ radio

rebuild:
	make clean && make -j 4
