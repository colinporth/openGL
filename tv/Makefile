TARGET    = tv.out
SRCS      = main.cpp \
	    ../../shared/utils/cLog.cpp \
	    ../../shared/glad/glad.c \
	    ../../shared/nanoVg/cVg.cpp \
	    ../../shared/nanoVg/cGlWindow.cpp \
	    ../../shared/dvb/cLinuxDvb.cpp \
	    ../../shared/dvb/cTransportStream.cpp \

BUILD_DIR = ./build
CLEAN_DIRS = $(BUILD_DIR) ../../shared/build
#
#
OBJS      = $(SRCS:%=$(BUILD_DIR)/%.o)
DEPS      = $(OBJS:.o=.d)
LIBS      = -l dl -l pthread -l asound -l GL -l glfw

CFLAGS = -Wall -Wno-unused-result \
	 -O2 -g \
	 -MMD -MP \
	 -D _LARGEFILE64_SOURCE -D _FILE_OFFSET_BITS=64

$(BUILD_DIR)/%.c.o: %.c
	mkdir -p $(dir $@)
	gcc -std=c11 $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.cpp.o: %.cpp
	mkdir -p $(dir $@)
	g++ -std=c++17 $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	g++ $(OBJS) -o $@ $(LIBS)

clean:
	rm -rf $(CLEAN_DIRS) $(TARGET)
rebuild:
	make clean && make -j 4

all: $(TARGET)

-include $(DEPS)
