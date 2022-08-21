TARGET = recpt1
RELEASE_VERSION = '1.0.0'

PREFIX   = /usr/local
CC       = gcc
CPPFLAGS = -I../driver -Wall -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64 -DB25
CFLAGS   = -O2 -g -pthread

PCSC_LIBS = `pkg-config libpcsclite --libs`
B25_LIBS = -larib25
LIBS     = $(PCSC_LIBS) $(B25_LIBS) -lm -lpthread
LDFLAGS  =

OBJS   = recpt1.o decoder.o mkpath.o
DEPEND = .deps

all: $(TARGET)

clean:
	rm -f $(OBJS) $(TARGET) $(DEPEND) version.h

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $(TARGET) $(OBJS) $(LIBS)

$(DEPEND): version.h
	$(CC) -MM $(OBJS:.o=.c) $(CPPFLAGS) > $@

version.h:
	revh=`hg parents --template 'const char *version = "r{rev}:{node|short} ({date|isodate})";\n' 2>/dev/null`; \
	if [ -n "$$revh" ] ; then \
		echo "$$revh" > $@; \
	else \
		echo "const char *version = \"$(RELEASE_VERSION)\";" > $@; \
	fi

install: $(TARGET)
	install -m 755 $(TARGET) $(PREFIX)/bin

-include .deps
