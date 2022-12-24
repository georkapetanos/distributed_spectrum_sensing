EXECUTABLES	= ciio

CC = gcc

CFLAGS = -O3 -Wall -g

LDFLAGS = -liio -lfftw3 -lm -lmosquitto

all: $(EXECUTABLES)

ciio: ciio.c spdetect.c spdetect.h mqtt.c mqtt.h
	$(CC) $(CFLAGS) ciio.c spdetect.c mqtt.c -o $(EXECUTABLES) $(LDFLAGS)

clean:
	rm ./ciio
