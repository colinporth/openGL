TARGET    = tvApp
SRCS      = tv.cpp \
	    ../../shared/utils/cLog.cpp \
	    ../../shared/fmt/format.cpp \
	    ../../shared/glad/glad.c \
	    ../../shared/vg/cVg.cpp \
	    ../../shared/vg/cAtlasText.cpp \
	    ../../shared/vg/cGlWindow.cpp \
	    ../../shared/dvb/cDvbUtils.cpp \
	    ../../shared/dvb/cDvb.cpp \
	    ../../shared/dvb/cTsDvb.cpp \
	    ../../shared/dvb/cTransportStream.cpp \

BUILD_DIR = ./build
CLEAN_DIRS = $(BUILD_DIR) ./shared
LIBS      = -no-pie \
	    -ldl -lbfd -lpthread -lGL -lglfw
#
#
OBJS      = $(SRCS:%=$(BUILD_DIR)/%.o)
DEPS      = $(OBJS:.o=.d)

CFLAGS = -Wall -Wno-unused-result \
	 -march=native \
	 -MMD -MP \
	 -D _LARGEFILE64_SOURCE -D _FILE_OFFSET_BITS=64 \
	 -ftree-vectorize \
	 -g -O2
#
MACHINE   = $(shell uname -m 2>&1)
ifeq "$(MACHINE)" "armv7l"
	CFLAGS += -mfpu=neon -flax-vector-conversions
endif
ifeq "$(MACHINE)" "aarch64"
	CFLAGS += -flax-vector-conversions
endif
#
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
