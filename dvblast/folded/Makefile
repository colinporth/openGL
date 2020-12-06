VERSION_MAJOR = 3
VERSION_MINOR = 4
TOPDIR = `basename ${PWD}`
GIT_VER = $(shell git describe --tags --dirty --always 2>/dev/null)
uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')
deltacast_inc := $(shell sh -c 'test -f /usr/include/StreamMaster.h && echo -n Y')

CFLAGS ?= -fomit-frame-pointer -g
CFLAGS += -O3
CFLAGS += -Wall -Wformat-security -Wno-strict-aliasing
CFLAGS += -DVERSION=\"$(VERSION_MAJOR).$(VERSION_MINOR)\"
CFLAGS += -DVERSION_MAJOR=$(VERSION_MAJOR)
CFLAGS += -DVERSION_MINOR=$(VERSION_MINOR)

ifneq "$(GIT_VER)" ""
CFLAGS += -DVERSION_EXTRA=\"git-$(GIT_VER)\"
else
CFLAGS += -DVERSION_EXTRA=\"release\"
endif

LDLIBS_DVBLAST += -lrt -lpthread -lev

OBJ_DVBLAST = dvblast.o util.o dvb.o demux.o output.o comm.o

ifndef V
Q = @
endif

CLEAN_OBJS = dvblast $(OBJ_DVBLAST)
INSTALL_BIN = dvblast

PREFIX ?= /usr/local
BIN = $(subst //,/,$(DESTDIR)/$(PREFIX)/bin)

all: dvblast dvblastctl

.PHONY: clean install uninstall dist

%.o: %.c Makefile dvblast.h comm.h util.h output.h demux.h
	@echo "CC      $<"
	$(Q)$(CROSS)$(CC) $(CFLAGS) $(CPPFLAGS) -c $<

dvblast: $(OBJ_DVBLAST)
	@echo "LINK    $@"
	$(Q)$(CROSS)$(CC) $(LDFLAGS) -o $@ $(OBJ_DVBLAST)  $(LDLIBS)

clean:
	@echo "CLEAN   $(CLEAN_OBJS)"
	$(Q)rm -f $(CLEAN_OBJS)

install: all
	@install -d "$(BIN)"
	@echo "INSTALL $(INSTALL_BIN) -> $(BIN)"
	$(Q)install dvblast  "$(BIN)"
