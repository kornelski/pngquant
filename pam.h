/* pam.h - header file for libppm portable pixmap library (alpha extensions)
 * [from ppm.h, Greg Roelofs, 27 July 1997]
 */

#ifndef _PAM_H_
#define _PAM_H_

#include "pgm.h"
#include "ppm.h"

typedef struct
    {
    pixval r, g, b, a;
    } apixel;
#define PAM_GETR(p) ((p).r)
#define PAM_GETG(p) ((p).g)
#define PAM_GETB(p) ((p).b)
#define PAM_GETA(p) ((p).a)

/************* added definitions *****************/
#define PAM_PUTR(p,red) ((p).r = (red))
#define PAM_PUTG(p,grn) ((p).g = (grn))
#define PAM_PUTB(p,blu) ((p).b = (blu))
#define PAM_PUTA(p,alf) ((p).a = (alf))
/**************************************************/

#define PAM_ASSIGN(p,red,grn,blu,alf) do { (p).r = (red); (p).g = (grn); (p).b = (blu); (p).a = (alf); } while ( 0 )
#define PAM_EQUAL(p,q) ( (p).r == (q).r && (p).g == (q).g && (p).b == (q).b && (p).a == (q).a )


/* Declarations of routines. */

#define pam_init pgm_init

#define pam_allocarray( cols, rows ) ((apixel**) pm_allocarray( cols, rows, sizeof(apixel) ))
#define pam_allocrow( cols ) ((apixel*) pm_allocrow( cols, sizeof(apixel) ))
#define pam_freearray( pixels, rows ) pm_freearray( (char**) pixels, rows )
#define pam_freerow( pixelrow ) pm_freerow( (char*) pixelrow )

apixel** pam_readpam ARGS(( FILE* file, int* colsP, int* rowsP, pixval* maxvalP ));
#define pam_readpaminit ppm_readppminit
void pam_readpamrow ARGS(( FILE* file, apixel* pixelrow, int cols, pixval maxval, int format ));

void pam_writepam ARGS(( FILE* file, apixel** pixels, int cols, int rows, pixval maxval, int forceplain ));
void pam_writepaminit ARGS(( FILE* file, int cols, int rows, pixval maxval, int forceplain ));
void pam_writepamrow ARGS(( FILE* file, apixel* pixelrow, int cols, pixval maxval, int forceplain ));


/* Color scaling macro -- to make writing ppmtowhatever easier. */

#define PAM_DEPTH(newp,p,oldmaxval,newmaxval) \
    PAM_ASSIGN( (newp), \
	( (int) PAM_GETR(p) * (newmaxval) + (oldmaxval) / 2 ) / (oldmaxval), \
	( (int) PAM_GETG(p) * (newmaxval) + (oldmaxval) / 2 ) / (oldmaxval), \
	( (int) PAM_GETB(p) * (newmaxval) + (oldmaxval) / 2 ) / (oldmaxval), \
	( (int) PAM_GETA(p) * (newmaxval) + (oldmaxval) / 2 ) / (oldmaxval) )

#endif /*_PAM_H_*/
