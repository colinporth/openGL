CC = gcc
CXX = g++

# -g -O2
CFLAGS   += -Wall
CXXFLAGS += -Wall -std=c++11

INCLUDES +=

SRC       = tvGrab.cpp \
	    ../../shared/utils/cLog.cpp \
	    ../../shared/dvb/cLinuxDvb.cpp \
	    ../../shared/nanoVg/cGlWindow.cpp \
	    ../../shared/nanoVg/cVg.cpp \
	    ../../shared/nanoVg/glad.c

OBJS     += $(filter %.o,$(SRC:.cpp=.o)) $(filter %.o,$(SRC:.c=.o))

LDFLAGS  += -lGL -lglfw -lpthread -ldl

all: tv Makefile

%.o: %.c Makefile
	@rm -f $@
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

%.o: %.cpp  Makefile
	@rm -f $@
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

tv: $(OBJS) Makefile
	$(CXX) $(LDFLAGS) -o tv $(OBJS)

.PHONY: clean rebuild

clean:
	rm -f *.o *.~* %*~ tv

rebuild:
	make clean && make
