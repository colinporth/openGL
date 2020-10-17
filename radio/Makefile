TARGET    = app
SRCS      = radio.cpp \
	    ../../shared/utils/cLog.cpp \
	    ../../shared/glad/glad.c \
	    ../../shared/net/cHttp.cpp \
	    ../../shared/nanoVg/cVg.cpp \
	    ../../shared/nanoVg/cAtlasText.cpp \
	    ../../shared/nanoVg/cGlWindow.cpp \
	    ../../shared/teensyAac/sbr.cpp \
	    ../../shared/teensyAac/huffman.cpp \
	    ../../shared/teensyAac/cAacDecoder.cpp \

BUILD_DIR = ./build
CLEAN_DIRS = $(BUILD_DIR) $(BUILD_DIR)../../shared
LIBS      = -no-pie \
	    -ldl -lbfd -lpthread -lGL -lglfw -lasound
#
#
OBJS      = $(SRCS:%=$(BUILD_DIR)/%.o)
DEPS      = $(OBJS:.o=.d)

CFLAGS = -Wall -Wno-unused-result \
	 -march=native \
	 -MMD -MP \
	 -D _LARGEFILE64_SOURCE -D _FILE_OFFSET_BITS=64 \
	 -g -O2 \

LD_VERSION = $(shell ld -v 2>&1 | sed -ne 's/.*\([0-9]\+\.[0-9]\+\).*/\1/p')
ifeq "$(LD_VERSION)" "2.34"
	CFLAGS += -D HAS_BINUTILS_234
endif

$(BUILD_DIR)/%.c.o: %.c
	mkdir -p $(dir $@)
	gcc -std=c11 $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.cpp.o: %.cpp
	mkdir -p $(dir $@)
	g++ -std=c++17 $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	g++ $(OBJS) -o $@ $(LIBS)

clean:
	rm -rf $(TARGET) $(CLEAN_DIRS)
rebuild:
	make clean && make -j 4

all: $(TARGET)

-include $(DEPS)
