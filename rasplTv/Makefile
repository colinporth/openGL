CC      := arm-linux-gnueabihf-gcc
CXX     := arm-linux-gnueabihf-g++

CFLAGS   += -std=c++0x \
	    -g -O2 \
	    -fPIC -ftree-vectorize -fomit-frame-pointer \
	    -Wall -Wno-psabi -Wno-deprecated-declarations \
	    -mabi=aapcs-linux -mno-apcs-stack-check -mno-sched-prolog \
	    -D PIC -D _REENTRANT \
	    -D _LARGEFILE64_SOURCE -D _FILE_OFFSET_BITS=64 -D OMX_SKIP64BIT \
	    -U _FORTIFY_SOURCE \
	    -mcpu=cortex-a53 -mtune=cortex-a53 -mfloat-abi=hard -mfpu=neon-fp-armv8
#           -march=armv6zk -mcpu=arm1176jzf-s -mtune=arm1176jzf-s -mfloat-abi=hard -mfpu=vfp
#           -mstructure-size-boundary=32 \

INCLUDES += -I/opt/vc/include \
	    -I/opt/vc/include/interface/vmcs_host \
	    -I/opt/vc/include/interface/vcos/pthreads \
	    -I/opt/vc/include/interface/vmcs_host/linux \
	    -I/usr/local/include \

LDFLAGS  += -L /opt/vc/lib/  \
	    -l pthread \
	    -l brcmGLESv2 -l brcmEGL -l bcm_host \
	    -l vcos -l vchostif -l vchiq_arm -l openmaxil \
	    -l asound \
	    -l rt -l m -l dl \

OBJS     += $(filter %.o,$(SRC:.cpp=.o))

SRC       = tv.cpp \
	    ../shared/dvb/cLinuxDvb.cpp \
	    ../shared/dvb/cTransportStream.cpp \
	    ../shared/utils/cLog.cpp \
	    ../shared/utils/cKeyboard.cpp \
	    ../shared/nanoVg/cRaspWindow.cpp \
	    ../shared/nanoVg/cVg.cpp \

all: tv

%.o: %.c
	@rm -f $@
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

%.o: %.cpp
	@rm -f $@
	$(CXX) $(CFLAGS) $(INCLUDES) -c $< -o $@

tv:    $(OBJS)
	$(CXX) $(LDFLAGS) -o tv $(OBJS)

clean:
	rm -f *.o
	rm -f *.log
	rm -f tv

.PHONY: clean rebuild

rebuild:
	make clean && make
