CC = gcc
CXX = g++

# -g -O2
CFLAGS += -Wall
CXXFLAGS += -Wall -std=c++11

INCLUDES +=

SRC       = hlsPlayer.cpp \
	    ../../shared/utils/cLog.cpp \
	    ../../shared/teensyAac/huffman.cpp \
	    ../../shared/teensyAac/cAacDecoder.cpp \
	    ../../shared/teensyAac/sbr.cpp \
	    ../../shared/nanoVg/cGlWindow.cpp \
	    ../../shared/nanoVg/cVg.cpp \
	    ../../shared/glad/glad.c \

OBJS     += $(filter %.o,$(SRC:.cpp=.o)) $(filter %.o,$(SRC:.c=.o))

LDFLAGS  += -l asound -l glfw -l dl -l pthread -l GL
#LDFLAGS  += -lasound -lglfw -lrt -lm -ldl -lX11 -lpthread -lxcb -lXau -lXdmcp -lGL

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
	make clean && make
