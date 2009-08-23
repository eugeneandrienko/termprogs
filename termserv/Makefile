CC=gcc
SOURCES=termserv.c net.c
OBJECTS=termserv.o net.o
BINARY=termserv
LIBS=-lc
CFLAGS=-c -Wall
LDFLAGS=-o $(BINARY)

all: objs
	$(CC) $(LDFLAGS) $(OBJECTS) $(LIBS)

objs: $(SOURCES)
	$(CC) $(CFLAGS) $(SOURCES)

.PHONY: clean
clean:
	rm -rf $(BINARY) \
		$(OBJECTS)

.PHONY: debug
debug:
	$(CC) $(CFLAGS) -ggdb -DDEBUG $(SOURCES)
	$(CC) $(LDFLAGS) $(OBJECTS) $(LIBS)

