CC=gcc
SOURCES=termanage.c net.c serv_pckt.c manag_pckt.c data_pckt.c
OBJFILES=termanage.o net.o serv_pckt.o manag_pckt.o data_pckt.o
BINARY=termanage
CFLAGS=-c -Wall
LDFAGS=-o $(BINARY)
LIBS=-lc -lssl

all: make_objs
	$(CC) $(LDFAGS) $(OBJFILES) $(LIBS)

make_objs: $(SOURCES)
	$(CC) $(CFLAGS) $(SOURCES)

.PHONY: clean
clean:
	rm -rf $(BINARY) \
		$(OBJFILES)

