
#include "libimagequant.h"
#include "pam.h"
#include "blur.h"

/*
 Blurs image horizontally (width 2*size+1) and writes it transposed to dst (called twice gives 2d blur)
 */
static void transposing_1d_blur(unsigned char *restrict src, unsigned char *restrict dst, unsigned int width, unsigned int height, const unsigned int size)
{
    for(unsigned int j=0; j < height; j++) {
        unsigned char *restrict row = src + j*width;

        // accumulate sum for pixels outside line
        unsigned int sum;
        sum = row[0]*size;
        for(unsigned int i=0; i < size; i++) {
            sum += row[i];
        }

        // blur with left side outside line
        for(unsigned int i=0; i < size; i++) {
            sum -= row[0];
            sum += row[i+size];

            dst[i*height + j] = sum / (size*2);
        }

        for(unsigned int i=size; i < width-size; i++) {
            sum -= row[i-size];
            sum += row[i+size];

            dst[i*height + j] = sum / (size*2);
        }

        // blur with right side outside line
        for(unsigned int i=width-size; i < width; i++) {
            sum -= row[i-size];
            sum += row[width-1];

            dst[i*height + j] = sum / (size*2);
        }
    }
}

/**
 * Picks maximum of neighboring pixels (blur + lighten)
 */
LIQ_PRIVATE void max3(unsigned char *src, unsigned char *dst, unsigned int width, unsigned int height)
{
    for(unsigned int j=0; j < height; j++) {
        const unsigned char *row = src + j*width,
        *prevrow = src + (j > 1 ? j-1 : 0)*width,
        *nextrow = src + MIN(height-1,j+1)*width;

        unsigned char prev,curr=row[0],next=row[0];

        for(unsigned int i=0; i < width-1; i++) {
            prev=curr;
            curr=next;
            next=row[i+1];

            unsigned char t1 = MAX(prev,next);
            unsigned char t2 = MAX(nextrow[i],prevrow[i]);
            *dst++ = MAX(curr,MAX(t1,t2));
        }
        unsigned char t1 = MAX(curr,next);
        unsigned char t2 = MAX(nextrow[width-1],prevrow[width-1]);
        *dst++ = MAX(t1,t2);
    }
}

/**
 * Picks minimum of neighboring pixels (blur + darken)
 */
LIQ_PRIVATE void min3(unsigned char *src, unsigned char *dst, unsigned int width, unsigned int height)
{
    for(unsigned int j=0; j < height; j++) {
        const unsigned char *row = src + j*width,
        *prevrow = src + (j > 1 ? j-1 : 0)*width,
        *nextrow = src + MIN(height-1,j+1)*width;

        unsigned char prev,curr=row[0],next=row[0];

        for(unsigned int i=0; i < width-1; i++) {
            prev=curr;
            curr=next;
            next=row[i+1];

            unsigned char t1 = MIN(prev,next);
            unsigned char t2 = MIN(nextrow[i],prevrow[i]);
            *dst++ = MIN(curr,MIN(t1,t2));
        }
        unsigned char t1 = MIN(curr,next);
        unsigned char t2 = MIN(nextrow[width-1],prevrow[width-1]);
        *dst++ = MIN(t1,t2);
    }
}

/*
 Filters src image and saves it to dst, overwriting tmp in the process.
 Image must be width*height pixels high. Size controls radius of box blur.
 */
LIQ_PRIVATE void blur(unsigned char *src, unsigned char *tmp, unsigned char *dst, unsigned int width, unsigned int height, unsigned int size)
{
    assert(size > 0);
    if (width < 2*size+1 || height < 2*size+1) return;
    transposing_1d_blur(src, tmp, width, height, size);
    transposing_1d_blur(tmp, dst, height, width, size);
}
