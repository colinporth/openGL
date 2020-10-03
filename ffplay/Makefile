ifeq "$(PWD)" "/mnt/c/projects/ffplay"
  BASE       = /mnt/c/projects
else
  BASE       = /home/pi
endif

TARGET     = play
SRCS       = play.cpp cmdutils.c

CFLAGS     = -Wall -g -O2\
	     -I$(BASE)/FFmpeg \
	     -I$(BASE)/FFmpeg/build \
	     -I$(BASE)/FFmpeg/fftools \
	     -I/usr/include/SDL2 \
	     -MMD -MP

LD_FLAGS  += $(BASE)/FFmpeg/build/libavdevice/libavdevice.a \
	     $(BASE)/FFmpeg/build/libavformat/libavformat.a \
	     $(BASE)/FFmpeg/build/libavfilter/libavfilter.a \
	     $(BASE)/FFmpeg/build/libavcodec/libavcodec.a \
	     $(BASE)/FFmpeg/build/libswresample/libswresample.a \
	     $(BASE)/FFmpeg/build/libswscale/libswscale.a \
	     $(BASE)/FFmpeg/build/libavutil/libavutil.a \
	     $(BASE)/FFmpeg/build/libpostproc/libpostproc.a \
	     -Wl,--as-needed \
	     -Wl,-z,noexecstack \
	     -Wl,--warn-common \
	     -Wl,-rpath-link=:libpostproc:libswresample:libswscale:libavfilter:libavdevice:libavformat:libavcodec:libavutil:libavresample  \
	     -lm -lz -pthread -lasound -lSDL2
#             -lsndio -lX11 -lXv -Xext -lxcb
#
BUILD_DIR  = ./build
CLEAN_DIRS = $(BUILD_DIR)

OBJS       = $(SRCS:%=$(BUILD_DIR)/%.o)
DEPS       = $(OBJS:.o=.d)

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
	g++ $(OBJS) -o $@ $(LD_FLAGS)

clean:
	rm -rf $(TARGET) $(CLEAN_DIRS)
rebuild:
	make clean && make -j 4

all: $(TARGET)

-include $(DEPS)
