EXECUTABLES	= ciio

CC = gcc

CFLAGS = -O3 -Wall -g

LDFLAGS = -liio -lfftw3 -lm

all: $(EXECUTABLES)

ciio: ciio.c spdetect.c spdetect.h
	$(CC) $(CFLAGS) ciio.c spdetect.c -o $(EXECUTABLES) $(LDFLAGS)

clean:
	rm ./ciio
