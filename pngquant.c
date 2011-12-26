/* pamquant.c - quantize the colors in an alphamap down to a specified number
**
** Copyright (C) 1989, 1991 by Jef Poskanzer.
** Copyright (C) 1997 by Greg Roelofs; based on an idea by Stefan Schneider.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#define VERSION "1.00 of 27 July 1997"

#include "pam.h"
#include "pamcmap.h"

#define MAXCOLORS 32767

#define LARGE_NORM
/* #define LARGE_LUM */   /* GRR 970727:  this isn't well-defined for RGBA */

/* #define REP_CENTER_BOX */
/* #define REP_AVERAGE_COLORS */
#define REP_AVERAGE_PIXELS

typedef struct box* box_vector;
struct box
    {
    int ind;
    int colors;
    int sum;
    };

static acolorhist_vector mediancut ARGS(( acolorhist_vector achv, int colors, int sum, pixval maxval, int newcolors ));
static int redcompare ARGS(( acolorhist_vector ch1, acolorhist_vector ch2 ));
static int greencompare ARGS(( acolorhist_vector ch1, acolorhist_vector ch2 ));
static int bluecompare ARGS(( acolorhist_vector ch1, acolorhist_vector ch2 ));
static int alphacompare ARGS(( acolorhist_vector ch1, acolorhist_vector ch2 ));
static int sumcompare ARGS(( box_vector b1, box_vector b2 ));

int
main( argc, argv )
    int argc;
    char* argv[];
    {
    FILE* ifp;
    apixel** apixels;
    apixel** mapapixels;
    register apixel* pP;
    int argn, rows, cols, maprows, mapcols, row;
    register int col, limitcol;
    pixval maxval, newmaxval, mapmaxval;
    int newcolors, colors;
    register int ind;
    acolorhist_vector achv, acolormap;
    acolorhash_table acht;
    gray **pgmrows;
    pixel *ppmrow;
    int floyd;
    int usehash;
    long* thisrerr;
    long* nextrerr;
    long* thisgerr;
    long* nextgerr;
    long* thisberr;
    long* nextberr;
    long* thisaerr;
    long* nextaerr;
    long* temperr;
    register long sr, sg, sb, sa, err;
#define FS_SCALE 1024
    int fs_direction;
    int x;
    char* usage = "[-floyd|-fs] <ncolors> [pamfile]\n                 [-floyd|-fs] -map mapfile [pamfile]";


    ppm_init( &argc, argv );

    argn = 1;
    floyd = 0;
    mapapixels = (apixel**) 0;

    while ( argn < argc && argv[argn][0] == '-' && argv[argn][1] != '\0' )
	{
	if ( pm_keymatch( argv[argn], "-fs", 2 ) ||
	     pm_keymatch( argv[argn], "-floyd", 2 ) )
	    floyd = 1;
	else if ( pm_keymatch( argv[argn], "-nofs", 2 ) ||
	     pm_keymatch( argv[argn], "-nofloyd", 2 ) )
	    floyd = 0;
	else if ( pm_keymatch( argv[argn], "-map", 2 ) )
	    {
	    ++argn;
	    if ( argn == argc )
		pm_usage( usage );
	    ifp = pm_openr( argv[argn] );
	    mapapixels = pam_readpam( ifp, &mapcols, &maprows, &mapmaxval );
	    pm_close( ifp );
	    if ( mapcols == 0 || maprows == 0 )
		pm_error( "null colormap??" );
	    }
	else
	    {
	    fprintf( stderr, "pamquant, version %s, by Greg Roelofs.\n",
	      VERSION );
	    pm_usage( usage );
	    }
	++argn;
	}

    if ( mapapixels == (apixel**) 0 )
	{
	if ( argn == argc )
	    pm_usage( usage );
	if ( sscanf( argv[argn], "%d", &newcolors ) != 1 )
	    pm_usage( usage );
	if ( newcolors <= 1 )
	    pm_error( "number of colors must be > 1" );
	++argn;
	}

    if ( argn != argc )
	{
	ifp = pm_openr( argv[argn] );
	++argn;
	}
    else
	ifp = stdin;

    if ( argn != argc )
	pm_usage( usage );

    /*
    ** Step 1: read in the image + alpha channel.
    */
    apixels = pam_readpam( ifp, &cols, &rows, &maxval );
    pm_close( ifp );


    if ( mapapixels == (apixel**) 0 )
	{
	/*
	** Step 2: attempt to make a histogram of the colors, unclustered.
	** If at first we don't succeed, lower maxval to increase color
	** coherence and try again.  This will eventually terminate, with
	** maxval at worst 15, since 32^3 is approximately MAXCOLORS.
	*/
	for ( ; ; )
	    {
	    pm_message( "making histogram..." );
	    achv = pam_computeacolorhist(
		apixels, cols, rows, MAXCOLORS, &colors );
	    if ( achv != (acolorhist_vector) 0 )
		break;
	    pm_message( "too many colors!" );
	    newmaxval = maxval / 2;
	    pm_message(
	  "scaling colors from maxval=%d to maxval=%d to improve clustering...",
		    maxval, newmaxval );
	    for ( row = 0; row < rows; ++row )
		for ( col = 0, pP = apixels[row]; col < cols; ++col, ++pP )
		    PAM_DEPTH( *pP, *pP, maxval, newmaxval );
	    maxval = newmaxval;
	    }
	pm_message( "%d colors found", colors );

	/*
	** Step 3: apply median-cut to histogram, making the new acolormap.
	*/
	pm_message( "choosing %d colors...", newcolors );
	acolormap = mediancut( achv, colors, rows * cols, maxval, newcolors );
	pam_freeacolorhist( achv );
	}
    else
	{
	/*
	** Alternate steps 2 & 3 : Turn mapapixels into a colormap.
	*/
	if ( mapmaxval != maxval )
	    {
	    if ( mapmaxval > maxval )
		pm_message( "rescaling colormap colors" );
	    for ( row = 0; row < maprows; ++row )
		for ( col = 0, pP = mapapixels[row]; col < mapcols; ++col, ++pP )
		    PAM_DEPTH( *pP, *pP, mapmaxval, maxval );
	    mapmaxval = maxval;
	    }
	acolormap = pam_computeacolorhist(
	    mapapixels, mapcols, maprows, MAXCOLORS, &newcolors );
	if ( acolormap == (acolorhist_vector) 0 )
	    pm_error( "too many colors in acolormap!" );
	pam_freearray( mapapixels, maprows );
	pm_message( "%d colors found in acolormap", newcolors );
	}

    /*
    ** Step 4: map the colors in the image to their closest match in the
    ** new colormap, and write 'em out.
    */
    pm_message( "mapping image to new colors..." );
    acht = pam_allocacolorhash( );
    usehash = 1;
    ppm_writeppminit( stdout, cols, rows, maxval, 0 );
    ppmrow = ppm_allocrow( cols );
    pgmrows = pgm_allocarray( cols, rows );  /* for appended alpha channel */
    if ( floyd )
	{
	/* Initialize Floyd-Steinberg error vectors. */
	thisrerr = (long*) pm_allocrow( cols + 2, sizeof(long) );
	nextrerr = (long*) pm_allocrow( cols + 2, sizeof(long) );
	thisgerr = (long*) pm_allocrow( cols + 2, sizeof(long) );
	nextgerr = (long*) pm_allocrow( cols + 2, sizeof(long) );
	thisberr = (long*) pm_allocrow( cols + 2, sizeof(long) );
	nextberr = (long*) pm_allocrow( cols + 2, sizeof(long) );
	thisaerr = (long*) pm_allocrow( cols + 2, sizeof(long) );
	nextaerr = (long*) pm_allocrow( cols + 2, sizeof(long) );
	srandom( (int) ( time( 0 ) ^ getpid( ) ) );
	for ( col = 0; col < cols + 2; ++col )
	    {
	    thisrerr[col] = random( ) % ( FS_SCALE * 2 ) - FS_SCALE;
	    thisgerr[col] = random( ) % ( FS_SCALE * 2 ) - FS_SCALE;
	    thisberr[col] = random( ) % ( FS_SCALE * 2 ) - FS_SCALE;
	    thisaerr[col] = random( ) % ( FS_SCALE * 2 ) - FS_SCALE;
	    /* (random errors in [-1 .. 1]) */
	    }
	fs_direction = 1;
	}
    for ( row = 0; row < rows; ++row )
	{
	if ( floyd )
	    for ( col = 0; col < cols + 2; ++col )
		nextrerr[col] = nextgerr[col] = nextberr[col] = nextaerr[col] = 0;
	if ( ( ! floyd ) || fs_direction )
	    {
	    col = 0;
	    limitcol = cols;
	    pP = apixels[row];
	    }
	else
	    {
	    col = cols - 1;
	    limitcol = -1;
	    pP = &(apixels[row][col]);
	    }
	do
	    {
	    if ( floyd )
		{
		/* Use Floyd-Steinberg errors to adjust actual color. */
		sr = PAM_GETR(*pP) + thisrerr[col + 1] / FS_SCALE;
		sg = PAM_GETG(*pP) + thisgerr[col + 1] / FS_SCALE;
		sb = PAM_GETB(*pP) + thisberr[col + 1] / FS_SCALE;
		sa = PAM_GETA(*pP) + thisaerr[col + 1] / FS_SCALE;
		if ( sr < 0 ) sr = 0;
		else if ( sr > maxval ) sr = maxval;
		if ( sg < 0 ) sg = 0;
		else if ( sg > maxval ) sg = maxval;
		if ( sb < 0 ) sb = 0;
		else if ( sb > maxval ) sb = maxval;
		if ( sa < 0 ) sa = 0;
		else if ( sa > maxval ) sa = maxval;
		PAM_ASSIGN( *pP, sr, sg, sb, sa );
		}

	    /* Check hash table to see if we have already matched this color. */
	    ind = pam_lookupacolor( acht, pP );
	    if ( ind == -1 )
		{ /* No; search acolormap for closest match. */
		register int i, r1, g1, b1, a1, r2, g2, b2, a2;
		register long dist, newdist;
		r1 = PAM_GETR( *pP );
		g1 = PAM_GETG( *pP );
		b1 = PAM_GETB( *pP );
		a1 = PAM_GETA( *pP );
		dist = 2000000000;
		for ( i = 0; i < newcolors; ++i )
		    {
		    r2 = PAM_GETR( acolormap[i].acolor );
		    g2 = PAM_GETG( acolormap[i].acolor );
		    b2 = PAM_GETB( acolormap[i].acolor );
		    a2 = PAM_GETA( acolormap[i].acolor );
		    newdist = ( r1 - r2 ) * ( r1 - r2 ) +  /* may overflow? */
			      ( g1 - g2 ) * ( g1 - g2 ) +
			      ( b1 - b2 ) * ( b1 - b2 ) +
			      ( a1 - a2 ) * ( a1 - a2 );
		    if ( newdist < dist )
			{
			ind = i;
			dist = newdist;
			}
		    }
		if ( usehash )
		    {
		    if ( pam_addtoacolorhash( acht, pP, ind ) < 0 )
			{
			pm_message(
		   "out of memory adding to hash table, proceeding without it");
			usehash = 0;
			}
		    }
		}

	    if ( floyd )
		{
		/* Propagate Floyd-Steinberg error terms. */
		if ( fs_direction )
		    {
		    err = ( sr - (long) PAM_GETR( acolormap[ind].acolor ) ) * FS_SCALE;
		    thisrerr[col + 2] += ( err * 7 ) / 16;
		    nextrerr[col    ] += ( err * 3 ) / 16;
		    nextrerr[col + 1] += ( err * 5 ) / 16;
		    nextrerr[col + 2] += ( err     ) / 16;
		    err = ( sg - (long) PAM_GETG( acolormap[ind].acolor ) ) * FS_SCALE;
		    thisgerr[col + 2] += ( err * 7 ) / 16;
		    nextgerr[col    ] += ( err * 3 ) / 16;
		    nextgerr[col + 1] += ( err * 5 ) / 16;
		    nextgerr[col + 2] += ( err     ) / 16;
		    err = ( sb - (long) PAM_GETB( acolormap[ind].acolor ) ) * FS_SCALE;
		    thisberr[col + 2] += ( err * 7 ) / 16;
		    nextberr[col    ] += ( err * 3 ) / 16;
		    nextberr[col + 1] += ( err * 5 ) / 16;
		    nextberr[col + 2] += ( err     ) / 16;
		    err = ( sa - (long) PAM_GETA( acolormap[ind].acolor ) ) * FS_SCALE;
		    thisaerr[col + 2] += ( err * 7 ) / 16;
		    nextaerr[col    ] += ( err * 3 ) / 16;
		    nextaerr[col + 1] += ( err * 5 ) / 16;
		    nextaerr[col + 2] += ( err     ) / 16;
		    }
		else
		    {
		    err = ( sr - (long) PAM_GETR( acolormap[ind].acolor ) ) * FS_SCALE;
		    thisrerr[col    ] += ( err * 7 ) / 16;
		    nextrerr[col + 2] += ( err * 3 ) / 16;
		    nextrerr[col + 1] += ( err * 5 ) / 16;
		    nextrerr[col    ] += ( err     ) / 16;
		    err = ( sg - (long) PAM_GETG( acolormap[ind].acolor ) ) * FS_SCALE;
		    thisgerr[col    ] += ( err * 7 ) / 16;
		    nextgerr[col + 2] += ( err * 3 ) / 16;
		    nextgerr[col + 1] += ( err * 5 ) / 16;
		    nextgerr[col    ] += ( err     ) / 16;
		    err = ( sb - (long) PAM_GETB( acolormap[ind].acolor ) ) * FS_SCALE;
		    thisberr[col    ] += ( err * 7 ) / 16;
		    nextberr[col + 2] += ( err * 3 ) / 16;
		    nextberr[col + 1] += ( err * 5 ) / 16;
		    nextberr[col    ] += ( err     ) / 16;
		    err = ( sa - (long) PAM_GETA( acolormap[ind].acolor ) ) * FS_SCALE;
		    thisaerr[col    ] += ( err * 7 ) / 16;
		    nextaerr[col + 2] += ( err * 3 ) / 16;
		    nextaerr[col + 1] += ( err * 5 ) / 16;
		    nextaerr[col    ] += ( err     ) / 16;
		    }
		}

	    *pP = acolormap[ind].acolor;

	    if ( ( ! floyd ) || fs_direction )
		{
		++col;
		++pP;
		}
	    else
		{
		--col;
		--pP;
		}
	    }
	while ( col != limitcol );

	if ( floyd )
	    {
	    temperr = thisrerr;
	    thisrerr = nextrerr;
	    nextrerr = temperr;
	    temperr = thisgerr;
	    thisgerr = nextgerr;
	    nextgerr = temperr;
	    temperr = thisberr;
	    thisberr = nextberr;
	    nextberr = temperr;
	    temperr = thisaerr;
	    thisaerr = nextaerr;
	    nextaerr = temperr;
	    fs_direction = ! fs_direction;
	    }

	/* split out PAM info into PPM and PGM rows; write PPM row now */
	for (x = 0;  x < cols;  ++x)
	    {
	    PPM_PUTR(ppmrow[x], PAM_GETR(apixels[row][x]));
	    PPM_PUTG(ppmrow[x], PAM_GETG(apixels[row][x]));
	    PPM_PUTB(ppmrow[x], PAM_GETB(apixels[row][x]));
	    pgmrows[row][x] = PAM_GETA(apixels[row][x]);
	    }

	ppm_writeppmrow( stdout, ppmrow, cols, maxval, 0 );
	}
    ppm_freerow( ppmrow );
    pam_freearray( apixels, rows );

    /* done writing PPM color info; now write PGM alpha info */
    pgm_writepgm( stdout, pgmrows, cols, rows, maxval, 0 );
    pgm_freearray( pgmrows, rows );

    pm_close( stdout );

    exit( 0 );
    }

/*
** Here is the fun part, the median-cut colormap generator.  This is based
** on Paul Heckbert's paper, "Color Image Quantization for Frame Buffer
** Display," SIGGRAPH '82 Proceedings, page 297.
*/

#if __STDC__
static acolorhist_vector
mediancut( acolorhist_vector achv, int colors, int sum, pixval maxval, int newcolors )
#else /*__STDC__*/
static acolorhist_vector
mediancut( achv, colors, sum, maxval, newcolors )
    acolorhist_vector achv;
    int colors, sum, newcolors;
    pixval maxval;
#endif /*__STDC__*/
    {
    acolorhist_vector acolormap;
    box_vector bv;
    register int bi, i;
    int boxes;

    bv = (box_vector) malloc( sizeof(struct box) * newcolors );
    acolormap =
	(acolorhist_vector) malloc( sizeof(struct acolorhist_item) * newcolors );
    if ( bv == (box_vector) 0 || acolormap == (acolorhist_vector) 0 )
	pm_error( "out of memory" );
    for ( i = 0; i < newcolors; ++i )
	PAM_ASSIGN( acolormap[i].acolor, 0, 0, 0, 0 );

    /*
    ** Set up the initial box.
    */
    bv[0].ind = 0;
    bv[0].colors = colors;
    bv[0].sum = sum;
    boxes = 1;

    /*
    ** Main loop: split boxes until we have enough.
    */
    while ( boxes < newcolors )
	{
	register int indx, clrs;
	int sm;
	register int minr, maxr, ming, mina, maxg, minb, maxb, maxa, v;
	int halfsum, lowersum;

	/*
	** Find the first splittable box.
	*/
	for ( bi = 0; bi < boxes; ++bi )
	    if ( bv[bi].colors >= 2 )
		break;
	if ( bi == boxes )
	    break;	/* ran out of colors! */
	indx = bv[bi].ind;
	clrs = bv[bi].colors;
	sm = bv[bi].sum;

	/*
	** Go through the box finding the minimum and maximum of each
	** component - the boundaries of the box.
	*/
	minr = maxr = PAM_GETR( achv[indx].acolor );
	ming = maxg = PAM_GETG( achv[indx].acolor );
	minb = maxb = PAM_GETB( achv[indx].acolor );
	mina = maxa = PAM_GETA( achv[indx].acolor );
	for ( i = 1; i < clrs; ++i )
	    {
	    v = PAM_GETR( achv[indx + i].acolor );
	    if ( v < minr ) minr = v;
	    if ( v > maxr ) maxr = v;
	    v = PAM_GETG( achv[indx + i].acolor );
	    if ( v < ming ) ming = v;
	    if ( v > maxg ) maxg = v;
	    v = PAM_GETB( achv[indx + i].acolor );
	    if ( v < minb ) minb = v;
	    if ( v > maxb ) maxb = v;
	    v = PAM_GETA( achv[indx + i].acolor );
	    if ( v < mina ) mina = v;
	    if ( v > maxa ) maxa = v;
	    }

	/*
	** Find the largest dimension, and sort by that component.  I have
	** included two methods for determining the "largest" dimension;
	** first by simply comparing the range in RGB space, and second
	** by transforming into luminosities before the comparison.  You
	** can switch which method is used by switching the commenting on
	** the LARGE_ defines at the beginning of this source file.
	*/
#ifdef LARGE_NORM
	if ( maxa - mina >= maxr - minr && maxa - mina >= maxg - ming && maxa - mina >= maxb - minb )
	    qsort(
		(char*) &(achv[indx]), clrs, sizeof(struct acolorhist_item),
		alphacompare );
	else if ( maxr - minr >= maxg - ming && maxr - minr >= maxb - minb )
	    qsort(
		(char*) &(achv[indx]), clrs, sizeof(struct acolorhist_item),
		redcompare );
	else if ( maxg - ming >= maxb - minb )
	    qsort(
		(char*) &(achv[indx]), clrs, sizeof(struct acolorhist_item),
		greencompare );
	else
	    qsort(
		(char*) &(achv[indx]), clrs, sizeof(struct acolorhist_item),
		bluecompare );
#endif /*LARGE_NORM*/
#ifdef LARGE_LUM
	{
	apixel p;
	float rl, gl, bl, al;

	PAM_ASSIGN(p, maxr - minr, 0, 0, 0);
	rl = PPM_LUMIN(p);
	PAM_ASSIGN(p, 0, maxg - ming, 0, 0);
	gl = PPM_LUMIN(p);
	PAM_ASSIGN(p, 0, 0, maxb - minb, 0);
	bl = PPM_LUMIN(p);
GRR: treat alpha as grayscale and assign (maxa - mina) to each of R, G, B?
     assign (maxa - mina)/3 to each?
     use alpha-fractional luminosity?  (normalized_alpha * lum(r,g,b))
        al = ...

	if ( al >= rl && al >= gl && al >= bl )
	    qsort(
		(char*) &(achv[indx]), clrs, sizeof(struct acolorhist_item),
		alphacompare );
	else if ( rl >= gl && rl >= bl )
	    qsort(
		(char*) &(achv[indx]), clrs, sizeof(struct acolorhist_item),
		redcompare );
	else if ( gl >= bl )
	    qsort(
		(char*) &(achv[indx]), clrs, sizeof(struct acolorhist_item),
		greencompare );
	else
	    qsort(
		(char*) &(achv[indx]), clrs, sizeof(struct acolorhist_item),
		bluecompare );
	}
#endif /*LARGE_LUM*/

	/*
	** Now find the median based on the counts, so that about half the
	** pixels (not colors, pixels) are in each subdivision.
	*/
	lowersum = achv[indx].value;
	halfsum = sm / 2;
	for ( i = 1; i < clrs - 1; ++i )
	    {
	    if ( lowersum >= halfsum )
		break;
	    lowersum += achv[indx + i].value;
	    }

	/*
	** Split the box, and sort to bring the biggest boxes to the top.
	*/
	bv[bi].colors = i;
	bv[bi].sum = lowersum;
	bv[boxes].ind = indx + i;
	bv[boxes].colors = clrs - i;
	bv[boxes].sum = sm - lowersum;
	++boxes;
	qsort( (char*) bv, boxes, sizeof(struct box), sumcompare );
	}

    /*
    ** Ok, we've got enough boxes.  Now choose a representative color for
    ** each box.  There are a number of possible ways to make this choice.
    ** One would be to choose the center of the box; this ignores any structure
    ** within the boxes.  Another method would be to average all the colors in
    ** the box - this is the method specified in Heckbert's paper.  A third
    ** method is to average all the pixels in the box.  You can switch which
    ** method is used by switching the commenting on the REP_ defines at
    ** the beginning of this source file.
    */
    for ( bi = 0; bi < boxes; ++bi )
	{
#ifdef REP_CENTER_BOX
	register int indx = bv[bi].ind;
	register int clrs = bv[bi].colors;
	register int minr, maxr, ming, maxg, minb, maxb, mina, maxa, v;

	minr = maxr = PAM_GETR( achv[indx].acolor );
	ming = maxg = PAM_GETG( achv[indx].acolor );
	minb = maxb = PAM_GETB( achv[indx].acolor );
	mina = maxa = PAM_GETA( achv[indx].acolor );
	for ( i = 1; i < clrs; ++i )
	    {
	    v = PAM_GETR( achv[indx + i].acolor );
	    minr = min( minr, v );
	    maxr = max( maxr, v );
	    v = PAM_GETG( achv[indx + i].acolor );
	    ming = min( ming, v );
	    maxg = max( maxg, v );
	    v = PAM_GETB( achv[indx + i].acolor );
	    minb = min( minb, v );
	    maxb = max( maxb, v );
	    v = PAM_GETA( achv[indx + i].acolor );
	    mina = min( mina, v );
	    maxa = max( maxa, v );
	    }
	PAM_ASSIGN(
	    acolormap[bi].acolor, ( minr + maxr ) / 2, ( ming + maxg ) / 2,
	    ( minb + maxb ) / 2, ( mina + maxa ) / 2 );
#endif /*REP_CENTER_BOX*/
#ifdef REP_AVERAGE_COLORS
	register int indx = bv[bi].ind;
	register int clrs = bv[bi].colors;
	register long r = 0, g = 0, b = 0, a = 0;

	for ( i = 0; i < clrs; ++i )
	    {
	    r += PAM_GETR( achv[indx + i].acolor );
	    g += PAM_GETG( achv[indx + i].acolor );
	    b += PAM_GETB( achv[indx + i].acolor );
	    a += PAM_GETA( achv[indx + i].acolor );
	    }
	r = r / clrs;
	g = g / clrs;
	b = b / clrs;
	a = a / clrs;
	PAM_ASSIGN( acolormap[bi].acolor, r, g, b, a );
#endif /*REP_AVERAGE_COLORS*/
#ifdef REP_AVERAGE_PIXELS
	register int indx = bv[bi].ind;
	register int clrs = bv[bi].colors;
	register long r = 0, g = 0, b = 0, a = 0, sum = 0;

	for ( i = 0; i < clrs; ++i )
	    {
	    r += PAM_GETR( achv[indx + i].acolor ) * achv[indx + i].value;
	    g += PAM_GETG( achv[indx + i].acolor ) * achv[indx + i].value;
	    b += PAM_GETB( achv[indx + i].acolor ) * achv[indx + i].value;
	    a += PAM_GETA( achv[indx + i].acolor ) * achv[indx + i].value;
	    sum += achv[indx + i].value;
	    }
	r = r / sum;
	if ( r > maxval ) r = maxval;	/* avoid math errors */
	g = g / sum;
	if ( g > maxval ) g = maxval;
	b = b / sum;
	if ( b > maxval ) b = maxval;
	a = a / sum;
	if ( a > maxval ) a = maxval;
	PAM_ASSIGN( acolormap[bi].acolor, r, g, b, a );
#endif /*REP_AVERAGE_PIXELS*/
	}

    /*
    ** All done.
    */
    return acolormap;
    }

static int
redcompare( ch1, ch2 )
    acolorhist_vector ch1, ch2;
    {
    return (int) PAM_GETR( ch1->acolor ) - (int) PAM_GETR( ch2->acolor );
    }

static int
greencompare( ch1, ch2 )
    acolorhist_vector ch1, ch2;
    {
    return (int) PAM_GETG( ch1->acolor ) - (int) PAM_GETG( ch2->acolor );
    }

static int
bluecompare( ch1, ch2 )
    acolorhist_vector ch1, ch2;
    {
    return (int) PAM_GETB( ch1->acolor ) - (int) PAM_GETB( ch2->acolor );
    }

static int
alphacompare( ch1, ch2 )
    acolorhist_vector ch1, ch2;
    {
    return (int) PAM_GETA( ch1->acolor ) - (int) PAM_GETA( ch2->acolor );
    }

static int
sumcompare( b1, b2 )
    box_vector b1, b2;
    {
    return b2->sum - b1->sum;
    }
