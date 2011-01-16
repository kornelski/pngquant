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


/* from pam.h */

typedef struct {
    unsigned char r, g, b, a;
} apixel;

/*
 typedef struct {
 ush r, g, b, a;
 } apixel16;
 */

/* from pamcmap.h */

typedef struct acolorhist_item *acolorhist_vector;
struct acolorhist_item {
    apixel acolor;
    int value;
    /*    int contrast;*/
};

typedef struct acolorhist_list_item *acolorhist_list;
struct acolorhist_list_item {
    acolorhist_list next;
    struct acolorhist_item ch;
};

typedef acolorhist_list *acolorhash_table;


typedef unsigned char pixval; /* GRR: hardcoded for now; later add 16-bit support */


acolorhist_vector pam_computeacolorhist(apixel** apixels, int cols, int rows, int maxacolors, int ignorebits, int* acolorsP);
void pam_freeacolorhist(acolorhist_vector achv);
acolorhash_table pam_allocacolorhash(void);
int pam_addtoacolorhash(acolorhash_table acht, apixel acolorP, int value);
int pam_lookupacolor(acolorhash_table acht, apixel acolorP);
