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

#include "pam.h"
#include "pamcmap.h"
/*
#include "libppm.h"
 */

#define HASH_SIZE 20023

#define pam_hashapixel(p) ( ( ( (long) PAM_GETR(p) * 33023 + (long) PAM_GETG(p) * 30013 + (long) PAM_GETB(p) * 27011 + (long) PAM_GETA(p) * 24007 ) & 0x7fffffff ) % HASH_SIZE )

acolorhist_vector
pam_computeacolorhist( apixels, cols, rows, maxacolors, acolorsP )
    apixel** apixels;
    int cols, rows, maxacolors;
    int* acolorsP;
    {
    acolorhash_table acht;
    acolorhist_vector achv;

    acht = pam_computeacolorhash( apixels, cols, rows, maxacolors, acolorsP );
    if ( acht == (acolorhash_table) 0 )
	return (acolorhist_vector) 0;
    achv = pam_acolorhashtoacolorhist( acht, maxacolors );
    pam_freeacolorhash( acht );
    return achv;
    }

void
pam_addtoacolorhist( achv, acolorsP, maxacolors, acolorP, value, position )
    acolorhist_vector achv;
    apixel* acolorP;
    int* acolorsP;
    int maxacolors, value, position;
    {
    int i, j;

    /* Search acolorhist for the color. */
    for ( i = 0; i < *acolorsP; ++i )
	if ( PAM_EQUAL( achv[i].acolor, *acolorP ) )
	    {
	    /* Found it - move to new slot. */
	    if ( position > i )
		{
		for ( j = i; j < position; ++j )
		    achv[j] = achv[j + 1];
		}
	    else if ( position < i )
		{
		for ( j = i; j > position; --j )
		    achv[j] = achv[j - 1];
		}
	    achv[position].acolor = *acolorP;
	    achv[position].value = value;
	    return;
	    }
    if ( *acolorsP < maxacolors )
	{
	/* Didn't find it, but there's room to add it; so do so. */
	for ( i = *acolorsP; i > position; --i )
	    achv[i] = achv[i - 1];
	achv[position].acolor = *acolorP;
	achv[position].value = value;
	++(*acolorsP);
	}
    }

acolorhash_table
pam_computeacolorhash( apixels, cols, rows, maxacolors, acolorsP )
    apixel** apixels;
    int cols, rows, maxacolors;
    int* acolorsP;
    {
    acolorhash_table acht;
    register apixel* pP;
    acolorhist_list achl;
    int col, row, hash;

    acht = pam_allocacolorhash( );
    *acolorsP = 0;

    /* Go through the entire image, building a hash table of colors. */
    for ( row = 0; row < rows; ++row )
	for ( col = 0, pP = apixels[row]; col < cols; ++col, ++pP )
	    {
	    hash = pam_hashapixel( *pP );
	    for ( achl = acht[hash]; achl != (acolorhist_list) 0; achl = achl->next )
		if ( PAM_EQUAL( achl->ch.acolor, *pP ) )
		    break;
	    if ( achl != (acolorhist_list) 0 )
		++(achl->ch.value);
	    else
		{
		if ( ++(*acolorsP) > maxacolors )
		    {
		    pam_freeacolorhash( acht );
		    return (acolorhash_table) 0;
		    }
		achl = (acolorhist_list) malloc( sizeof(struct acolorhist_list_item) );
		if ( achl == 0 )
		    pm_error( "out of memory computing hash table" );
		achl->ch.acolor = *pP;
		achl->ch.value = 1;
		achl->next = acht[hash];
		acht[hash] = achl;
		}
	    }
    
    return acht;
    }

acolorhash_table
pam_allocacolorhash( )
    {
    acolorhash_table acht;
    int i;

    acht = (acolorhash_table) malloc( HASH_SIZE * sizeof(acolorhist_list) );
    if ( acht == 0 )
	pm_error( "out of memory allocating hash table" );

    for ( i = 0; i < HASH_SIZE; ++i )
	acht[i] = (acolorhist_list) 0;

    return acht;
    }

int
pam_addtoacolorhash( acht, acolorP, value )
    acolorhash_table acht;
    apixel* acolorP;
    int value;
    {
    register int hash;
    register acolorhist_list achl;

    achl = (acolorhist_list) malloc( sizeof(struct acolorhist_list_item) );
    if ( achl == 0 )
	return -1;
    hash = pam_hashapixel( *acolorP );
    achl->ch.acolor = *acolorP;
    achl->ch.value = value;
    achl->next = acht[hash];
    acht[hash] = achl;
    return 0;
    }

acolorhist_vector
pam_acolorhashtoacolorhist( acht, maxacolors )
    acolorhash_table acht;
    int maxacolors;
    {
    acolorhist_vector achv;
    acolorhist_list achl;
    int i, j;

    /* Now collate the hash table into a simple acolorhist array. */
    achv = (acolorhist_vector) malloc( maxacolors * sizeof(struct acolorhist_item) );
    /* (Leave room for expansion by caller.) */
    if ( achv == (acolorhist_vector) 0 )
	pm_error( "out of memory generating histogram" );

    /* Loop through the hash table. */
    j = 0;
    for ( i = 0; i < HASH_SIZE; ++i )
	for ( achl = acht[i]; achl != (acolorhist_list) 0; achl = achl->next )
	    {
	    /* Add the new entry. */
	    achv[j] = achl->ch;
	    ++j;
	    }

    /* All done. */
    return achv;
    }

acolorhash_table
pam_acolorhisttoacolorhash( achv, colors )
    acolorhist_vector achv;
    int colors;
    {
    acolorhash_table acht;
    int i, hash;
    apixel acolor;
    acolorhist_list achl;

    acht = pam_allocacolorhash( );

    for ( i = 0; i < colors; ++i )
	{
	acolor = achv[i].acolor;
	hash = pam_hashapixel( acolor );
	for ( achl = acht[hash]; achl != (acolorhist_list) 0; achl = achl->next )
	    if ( PAM_EQUAL( achl->ch.acolor, acolor ) )
		pm_error(
		    "same acolor found twice - %d %d %d %d", PAM_GETR(acolor),
		    PAM_GETG(acolor), PAM_GETB(acolor), PAM_GETA(acolor) );
	achl = (acolorhist_list) malloc( sizeof(struct acolorhist_list_item) );
	if ( achl == (acolorhist_list) 0 )
	    pm_error( "out of memory" );
	achl->ch.acolor = acolor;
	achl->ch.value = i;
	achl->next = acht[hash];
	acht[hash] = achl;
	}

    return acht;
    }

int
pam_lookupacolor( acht, acolorP )
    acolorhash_table acht;
    apixel* acolorP;
    {
    int hash;
    acolorhist_list achl;

    hash = pam_hashapixel( *acolorP );
    for ( achl = acht[hash]; achl != (acolorhist_list) 0; achl = achl->next )
	if ( PAM_EQUAL( achl->ch.acolor, *acolorP ) )
	    return achl->ch.value;

    return -1;
    }

void
pam_freeacolorhist( achv )
    acolorhist_vector achv;
    {
    free( (char*) achv );
    }

void
pam_freeacolorhash( acht )
    acolorhash_table acht;
    {
    int i;
    acolorhist_list achl, achlnext;

    for ( i = 0; i < HASH_SIZE; ++i )
	for ( achl = acht[i]; achl != (acolorhist_list) 0; achl = achlnext )
	    {
	    achlnext = achl->next;
	    free( (char*) achl );
	    }
    free( (char*) acht );
    }
