EXECUTABLES	= ciio concentrator

CC = gcc

CFLAGS = -O3 -Wall -g

LDFLAGS = -liio -lfftw3 -lm -lmosquitto -lsqlite3

all: $(EXECUTABLES)

ciio: ciio.c spdetect.c spdetect.h mqtt.c mqtt.h
	$(CC) $(CFLAGS) ciio.c spdetect.c mqtt.c -o ciio $(LDFLAGS)

concentrator: concentrator.c mqtt.c mqtt.h
	$(CC) $(CFLAGS) -o concentrator concentrator.c mqtt.c -lmosquitto -lsqlite3

clean:
	rm ./ciio ./concentrator
