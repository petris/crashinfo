#
# Configuration
#

# Compile with libunwind support, which is needed for generating backtraces
CRASHINFO_WITH_LIBUNWIND ?= 1

# Select libunwind platform
CRASHINFO_WITH_LIBUNWIND_ARCH ?= generic

# Compile with debugging options (enables log_dbg)
CRASHINFO_WITH_DEBUG ?= 0

# Destination directory for "make install"
DESTDIR ?= /

#
# Build
#

ifneq ($(CRASHINFO_WITH_DEBUG), 1)
  override CFLAGS += -DNDEBUG
endif

ifeq ($(CRASHINFO_WITH_LIBUNWIND), 1)
  override CFLAGS += -DCRASHINFO_WITH_LIBUNWIND $(shell pkg-config --cflags --libs libunwind libunwind-coredump | sed s/generic/$(CRASHINFO_WITH_LIBUNWIND_ARCH)/g)
endif

.PHONY: all clean install

all: crashinfo

crashinfo: main.c log.c conf.c info.c proc.c unw.c util.c
	$(CC) $(CFLAGS) -Wall $^ -o $@ -lrt -lpthread

clean:
	if [ -f crashinfo ]; then rm crashinfo; fi

install: all
	install -d -m 0755 "$(DESTDIR)/bin"
	install -m 755 crashinfo "$(DESTDIR)/bin"
