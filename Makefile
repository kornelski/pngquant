# Makefile for pngquant
# GRR 20001222

CC=gcc

PNGINC = ../libpng
PNGLIB = ../libpng

ZINC = ../zlib
ZLIB = ../zlib

CFLAGS = -O3 -Wall -I. -I$(PNGINC) -I$(ZINC)
#LDFLAGS = -L$(PNGLIB) -lpng -L$(ZLIB) -lz -lm
LDFLAGS = $(PNGLIB)/libpng.a -L$(ZLIB) -lz -lm

OBJS = pngquant.o rwpng.o 

all: pngquant

pngquant: pngquant.o rwpng.o
	$(CC) -o pngquant $(OBJS) $(LDFLAGS)

