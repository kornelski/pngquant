/**
 ** Copyright (C) 1989, 1991 by Jef Poskanzer.
 ** Copyright (C) 1997, 2000, 2002 by Greg Roelofs; based on an idea by
 **                                Stefan Schneider.
 ** Copyright (C) 2009 by Kornel Lesinski.
 **
 ** Permission to use, copy, modify, and distribute this software and its
 ** documentation for any purpose and without fee is hereby granted, provided
 ** that the above copyright notice appear in all copies and that both that
 ** copyright notice and this permission notice appear in supporting
 ** documentation.  This software is provided "as is" without express or
 ** implied warranty.
 */

#include <stdio.h>
#include <stdlib.h>

#include "pam.h"

#define PAM_EQUAL(p,q) \
((p).r == (q).r && (p).g == (q).g && (p).b == (q).b && (p).a == (q).a)

static acolorhist_vector pam_acolorhashtoacolorhist(acolorhash_table acht, int maxacolors);
static acolorhash_table pam_computeacolorhash(apixel** apixels, int cols, int rows, int maxacolors, int ignorebits, int* acolorsP);
static void pam_freeacolorhash(acolorhash_table acht);


/*

 libpam3.c:
 pam_computeacolorhist( )
 NOTUSED pam_addtoacolorhist( )
 pam_computeacolorhash( )
 pam_allocacolorhash( )
 pam_addtoacolorhash( )
 pam_acolorhashtoacolorhist( )
 NOTUSED pam_acolorhisttoacolorhash( )
 pam_lookupacolor( )
 pam_freeacolorhist( )
 pam_freeacolorhash( )

 libpbm1.c:
 pm_freearray( )
 pm_allocrow( )

 pam.h:
 pam_freearray( )
 */


/*===========================================================================*/


/* libpam3.c - pam (portable alpha map) utility library part 3
 **
 ** Colormap routines.
 **
 ** Copyright (C) 1989, 1991 by Jef Poskanzer.
 ** Copyright (C) 1997 by Greg Roelofs.
 **
 ** Permission to use, copy, modify, and distribute this software and its
 ** documentation for any purpose and without fee is hereby granted, provided
 ** that the above copyright notice appear in all copies and that both that
 ** copyright notice and this permission notice appear in supporting
 ** documentation.  This software is provided "as is" without express or
 ** implied warranty.
 */

/*
 #include "pam.h"
 #include "pamcmap.h"
 */

#define HASH_SIZE 24137

#define pam_hashapixel(p) ( ( ( (long) (p).r + \
((long) (p).g << 13) + \
(long) (p).b * 8009 + \
(long) (p).a * 24133 ) ) % HASH_SIZE )

#define PAM_SCALE(p, oldmaxval, newmaxval) ((int)(p) == (oldmaxval) ? (newmaxval) : (int)(p) * ((newmaxval)+1) / (oldmaxval))

acolorhist_vector pam_computeacolorhist(apixel** apixels, int cols, int rows, int maxacolors, int ignorebits, int* acolorsP)
{
    acolorhash_table acht;
    acolorhist_vector achv;

    acht = pam_computeacolorhash(apixels, cols, rows, maxacolors, ignorebits, acolorsP);
    if (acht == (acolorhash_table) 0)
        return (acolorhist_vector) 0;
    achv = pam_acolorhashtoacolorhist(acht, maxacolors);
    pam_freeacolorhash(acht);
    return achv;
}

static acolorhash_table pam_computeacolorhash(apixel** apixels, int cols, int rows, int maxacolors, int ignorebits, int* acolorsP)
{
    acolorhash_table acht;
    acolorhist_list achl;
    int col, row, hash;
    int maxval = 255>>ignorebits;
    acht = pam_allocacolorhash( );
    *acolorsP = 0;

    /* Go through the entire image, building a hash table of colors. */
    for (row = 0; row < rows; ++row) {
        for (col = 0; col < cols; ++col) {

            apixel px = apixels[row][col];

            if (maxval != 255) {
                px.r = PAM_SCALE(px.r, 255, maxval); px.r = PAM_SCALE(px.r, maxval, 255);
                px.g = PAM_SCALE(px.g, 255, maxval); px.g = PAM_SCALE(px.g, maxval, 255);
                px.b = PAM_SCALE(px.b, 255, maxval); px.b = PAM_SCALE(px.b, maxval, 255);
                px.a = PAM_SCALE(px.a, 255, maxval); px.a = PAM_SCALE(px.a, maxval, 255);
            }

            hash = pam_hashapixel(px);
            for (achl = acht[hash]; achl != (acolorhist_list) 0; achl = achl->next)
                if (PAM_EQUAL(achl->ch.acolor, px))
                    break;
            if (achl != (acolorhist_list) 0) {
                ++(achl->ch.value);
            } else {
                if (++(*acolorsP) > maxacolors) {
                    pam_freeacolorhash(acht);
                    return (acolorhash_table) 0;
                }
                achl = (acolorhist_list) malloc(sizeof(struct acolorhist_list_item));
                if (achl == 0) {
                    fprintf(stderr, "  out of memory computing hash table\n");
                    exit(7);
                }
                achl->ch.acolor = px;
                achl->ch.value = 1;
                achl->next = acht[hash];
                acht[hash] = achl;
            }
        }

    }
    return acht;
}



acolorhash_table pam_allocacolorhash( )
{
    acolorhash_table acht;
    int i;

    acht = (acolorhash_table) malloc(HASH_SIZE * sizeof(acolorhist_list));
    if (acht == 0) {
        fprintf(stderr, "  out of memory allocating hash table\n");
        exit(8);
    }

    for (i = 0; i < HASH_SIZE; ++i)
        acht[i] = (acolorhist_list) 0;

    return acht;
}



int pam_addtoacolorhash(acolorhash_table acht, apixel* acolorP, int value)
{
    int hash;
    acolorhist_list achl;

    achl = (acolorhist_list) malloc(sizeof(struct acolorhist_list_item));
    if (achl == 0)
        return -1;
    hash = pam_hashapixel(*acolorP);
    achl->ch.acolor = *acolorP;
    achl->ch.value = value;
    achl->next = acht[hash];
    acht[hash] = achl;
    return 0;
}



static acolorhist_vector pam_acolorhashtoacolorhist(acolorhash_table acht, int maxacolors)
{
    acolorhist_vector achv;
    acolorhist_list achl;
    int i, j;

    /* Now collate the hash table into a simple acolorhist array. */
    achv = (acolorhist_vector) malloc(maxacolors * sizeof(struct acolorhist_item));
    /* (Leave room for expansion by caller.) */
    if (achv == (acolorhist_vector) 0) {
        fprintf(stderr, "  out of memory generating histogram\n");
        exit(9);
    }

    /* Loop through the hash table. */
    j = 0;
    for (i = 0; i < HASH_SIZE; ++i)
        for (achl = acht[i]; achl != (acolorhist_list) 0; achl = achl->next) {
            /* Add the new entry. */
            achv[j] = achl->ch;
            ++j;
        }

    /* All done. */
    return achv;
}




static void pam_freeacolorhash(acolorhash_table acht)
{
    int i;
    acolorhist_list achl, achlnext;

    for (i = 0; i < HASH_SIZE; ++i)
        for (achl = acht[i]; achl != (acolorhist_list) 0; achl = achlnext) {
            achlnext = achl->next;
            free((char*) achl);
        }
    free((char*) acht);
}



int pam_lookupacolor(acolorhash_table acht, apixel* acolorP)
{
    int hash;
    acolorhist_list achl;

    hash = pam_hashapixel(*acolorP);
    for (achl = acht[hash]; achl != (acolorhist_list) 0; achl = achl->next)
        if (PAM_EQUAL(achl->ch.acolor, *acolorP))
            return achl->ch.value;

    return -1;
}



void pam_freeacolorhist(acolorhist_vector achv)
{
    free((char*) achv);
}


