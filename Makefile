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

TARGETS := crashinfo crashinfo.1.gz

ifneq ($(CRASHINFO_WITH_DEBUG), 1)
  override CFLAGS += -DNDEBUG
endif

ifeq ($(CRASHINFO_WITH_LIBUNWIND), 1)
  override CFLAGS += -DCRASHINFO_WITH_LIBUNWIND $(shell pkg-config --cflags --libs libunwind libunwind-coredump | sed s/generic/$(CRASHINFO_WITH_LIBUNWIND_ARCH)/g)
endif

.PHONY: all clean install test

all: $(TARGETS)

crashinfo: main.c log.c conf.c info.c proc.c unw.c util.c
	$(CC) $(CFLAGS) -Werror $^ -o $@ -lrt -lpthread

%.gz: %
	gzip -9 < $< > $@

clean:
	for file in $(TARGETS); do if [ -f $$file ]; then rm $$file; fi; done

test: all
	make -C t test

install: all
	install -d -m 0755 "$(DESTDIR)/bin"
	install -m 0755 crashinfo "$(DESTDIR)/bin"
	install -d -m 0755 "$(DESTDIR)/share/man/man1"
	install -m 0644 crashinfo.1.gz "$(DESTDIR)/share/man/man1"
