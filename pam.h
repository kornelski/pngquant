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

#include <math.h>

/* from pam.h */

typedef struct {
    unsigned char r, g, b, a;
} rgb_pixel;

typedef struct {
    float r, g, b, a;
} f_pixel;

inline static f_pixel to_f(double gamma, rgb_pixel px)
{
    double r = pow((1.0+px.r)/256.0, 1.0/gamma), // Division by 255 creates rounding errors
           g = pow((1.0+px.g)/256.0, 1.0/gamma),
           b = pow((1.0+px.b)/256.0, 1.0/gamma),
           a = (1+px.a)/256.0;

    return (f_pixel){r,g,b,a};
}

inline static rgb_pixel to_rgb(double gamma, f_pixel px)
{
    if (px.a < 1.0/257.0) {   // px.a = 1+a/256
        return (rgb_pixel){0,0,0,0};
    }

    double r,g,b,a;

    // 257, because numbers are in range 1..256.9999… rounded down
    r = pow(px.r, gamma)*257.0 - 1.0;
    g = pow(px.g, gamma)*257.0 - 1.0;
    b = pow(px.b, gamma)*257.0 - 1.0;
    a = px.a*257.0 - 1.0;

    return (rgb_pixel){
        r>=255 ? 255 : (r<=0 ? 0 : r),
        g>=255 ? 255 : (g<=0 ? 0 : g),
        b>=255 ? 255 : (b<=0 ? 0 : b),
        a>=255 ? 255 : a,
    };
}

/*
 typedef struct {
 ush r, g, b, a;
 } apixel16;
 */

/* from pamcmap.h */

typedef struct acolorhist_item *acolorhist_vector;
struct acolorhist_item {
    f_pixel acolor;
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


acolorhist_vector pam_computeacolorhist(rgb_pixel*const* apixels, int cols, int rows, double gamma, int maxacolors, int ignorebits, int* acolorsP);
void pam_freeacolorhist(acolorhist_vector achv);
