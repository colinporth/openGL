TARGET    = tv.out
SRCS      = main.cpp \
	    ../../shared/utils/cLog.cpp \
	    ../../shared/glad/glad.c \
	    ../../shared/nanoVg/cVg.cpp \
	    ../../shared/nanoVg/cGlWindow.cpp \
	    ../../shared/dvb/cLinuxDvb.cpp \
	    ../../shared/dvb/cTransportStream.cpp \

BUILD_DIR = ./build
OBJS      = $(SRCS:%=$(BUILD_DIR)/%.o)
DEPS      = $(OBJS:.o=.d)
LIBS      = -l dl -l pthread -l asound -l GL -l glfw

COM_FLAGS = -Wall \
	    -O2 -g \
	    -MMD -MP \
	    -D _LARGEFILE64_SOURCE -D _FILE_OFFSET_BITS=64 \

INC_FLAGS =
CFLAGS   += -std=c11 $(COM_FLAGS) $(INC_FLAGS)
CXXFLAGS += -std=c++17 $(COM_FLAGS) $(INC_FLAGS)

$(BUILD_DIR)/%.c.o: %.c
	mkdir -p $(dir $@)
	gcc $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.cpp.o: %.cpp
	mkdir -p $(dir $@)
	g++ $(CXXFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	g++ $(OBJS) -o $@ $(LIBS)

clean:
	rm -rf $(BUILD_DIR) $(TARGET)
rebuild:
	make clean && make -j 4

all: $(TARGET)

-include $(DEPS)
