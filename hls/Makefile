TARGET    = app
SRCS      = hls.cpp \
	    ../../shared/utils/cLoaderPlayer.cpp \
	    ../../shared/utils/cVideoDecode.cpp \
	    ../../shared/utils/cSong.cpp \
	    ../../shared/utils/cLog.cpp \
	    ../../shared/glad/glad.c \
	    ../../shared/net/cHttp.cpp \
	    ../../shared/nanoVg/cVg.cpp \
	    ../../shared/nanoVg/cAtlasText.cpp \
	    ../../shared/nanoVg/cGlWindow.cpp \
	    ../../shared/decoders/cAudioDecode.cpp \
	    ../../shared/decoders/cAacDecoder.cpp \
	    ../../shared/decoders/cMp3Decoder.cpp \
	    ../../shared/kissFft/kiss_fft.c \
	    ../../shared/kissFft/kiss_fftr.c \

BUILD_DIR = ./build
CLEAN_DIRS = $(BUILD_DIR) $(BUILD_DIR)../../shared
LIBS      = -no-pie \
	    -ldl -lbfd -lpthread -lGL -lglfw -lasound \
	    `pkg-config --libs libavcodec` \
	    `pkg-config --libs libavformat` \
	    `pkg-config --libs libavutil` \
	    `pkg-config --libs libswscale` \
#
#
OBJS      = $(SRCS:%=$(BUILD_DIR)/%.o)
DEPS      = $(OBJS:.o=.d)
#
TYPE      = $(shell uname -p 2>&1)
#
#
CFLAGS = -Wall -Wno-unused-result \
	 -D _LARGEFILE64_SOURCE -D _FILE_OFFSET_BITS=64 \
	 -MMD -MP \
	 `pkg-config --cflags libavcodec` \
	 `pkg-config --cflags libavformat` \
	 `pkg-config --cflags libswscale` \
	 -g -O2
#
ifeq "$(TYPE)" "x86_64"
	CFLAGS += -march=sandybridge
endif
#         -march=goldmont-plus

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
