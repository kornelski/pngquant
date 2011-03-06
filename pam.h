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

/*
 Converts 8-bit RGB to linear RGB premultiplied by alpha.
 Premultiplied color space is much better for blending of semitransparent colors.
 */
inline static f_pixel to_f(float gamma, rgb_pixel px)
{
    float r = powf(px.r/255.0f, 1.0f/gamma),
          g = powf(px.g/255.0f, 1.0f/gamma),
          b = powf(px.b/255.0f, 1.0f/gamma),
          a = px.a/255.0f;

    return (f_pixel){r*a,g*a,b*a,a};
}

inline static rgb_pixel to_rgb(float gamma, f_pixel px)
{
    if (px.a < 1.0/256.0) {
        return (rgb_pixel){0,0,0,0};
    }

    float r,g,b,a;

    // 256, because numbers are in range 1..255.9999… rounded down
    r = powf(px.r/px.a, gamma)*256.0f;
    g = powf(px.g/px.a, gamma)*256.0f;
    b = powf(px.b/px.a, gamma)*256.0f;
    a = px.a*256.0;

    return (rgb_pixel){
        r>=255 ? 255 : (r<=0 ? 0 : r),
        g>=255 ? 255 : (g<=0 ? 0 : g),
        b>=255 ? 255 : (b<=0 ? 0 : b),
        a>=255 ? 255 : a,
    };
}

/* from pamcmap.h */

typedef struct acolorhist_item *acolorhist_vector;
struct acolorhist_item {
    f_pixel acolor;
    int value;
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
