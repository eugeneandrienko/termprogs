CC=gcc
SOURCES=termemu.c net.c data_pckt.c serv_pckt.c
OBJFILES=termemu.o net.o data_pckt.o serv_pckt.o
BINARY=termemu
CFLAGS=-c -Wall
LDFAGS=-o $(BINARY)
LIBS=-lc -lm -lssl

all: make_objs
	$(CC) $(LDFAGS) $(OBJFILES) $(LIBS)

make_objs: $(SOURCES)
	$(CC) $(CFLAGS) $(SOURCES)

.PHONY: clean
clean:
	rm -rf $(BINARY) \
		$(OBJFILES)

