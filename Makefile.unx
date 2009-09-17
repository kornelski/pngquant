# Makefile for pngquant
# GRR 20020405

CC=gcc

PNGINC = ../libpng
PNGLIB = ../libpng

ZINC = ../zlib
ZLIB = ../zlib

CFLAGS = -O3 -Wall -I. -I$(PNGINC) -I$(ZINC) -funroll-loops -fomit-frame-pointer

#LDFLAGS = -L$(PNGLIB) -lpng -L$(ZLIB) -lz -lm
LDFLAGS = $(PNGLIB)/libpng.a -L$(ZLIB) -lz -lm
#LDFLAGS = $(PNGLIB)/libpng.a $(ZLIB)/libz.a -lm

OBJS = pngquant.o rwpng.o 

all: pngquant

pngquant: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

clean:
	rm -f pngquant $(OBJS)
