/* libpam1.c - pam utility library part 1
**
** Copyright (C) 1997 by Greg Roelofs.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#include "pam.h"
#include "ppm.h"
#include "pgm.h"
/*
#include "libpam.h"
#include "libppm.h"
#include "libpgm.h"
#include "pbm.h"
#include "libpbm.h"
 */

apixel** pam_readpam(file, colsP, rowsP, maxvalP)
    FILE* file;
    int* colsP;
    int* rowsP;
    pixval* maxvalP;
{
    apixel **apixels;
    apixel *pA;
    pixel *row, *pP;
    gray *grayrow, *pG;
    int x, y;
    int arows, acols;
    pixval amaxval;
    int format;

    ppm_readppminit(file, colsP, rowsP, maxvalP, &format);

    apixels = pam_allocarray(*colsP, *rowsP);
    row = ppm_allocrow(*colsP);

    for (y = 0;  y < *rowsP;  ++y) {
        ppm_readppmrow(file, row, *colsP, *maxvalP, format);
        for (pA = apixels[y], pP = row, x = 0;  x < *colsP;  ++x, ++pP, ++pA) {
            PAM_PUTR(*pA, PPM_GETR(*pP));
            PAM_PUTG(*pA, PPM_GETG(*pP));
            PAM_PUTB(*pA, PPM_GETB(*pP));
        }
    }

    pgm_readpgminit(file, &acols, &arows, &amaxval, &format);

    if (acols != *colsP || arows != *rowsP)
	pm_error( "image and alpha-channel dimensions must match" );

    grayrow = pgm_allocrow(*colsP);

    if (amaxval > *maxvalP)
        pm_message("rescaling alpha-channel values");

    for (y = 0;  y < *rowsP;  ++y) {
        pgm_readpgmrow(file, grayrow, acols, amaxval, format);
        for (pA = apixels[y], pG = grayrow, x = 0;  x < acols;  ++x, ++pG, ++pA)
        {
            if (amaxval != *maxvalP)  /* rescale alpha values to match image */
                *pG = ((int)(*pG) * (*maxvalP) + (amaxval>>1)) / amaxval;
            PAM_PUTA(*pA, *pG);
        }
    }

    return apixels;
}
