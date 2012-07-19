
#include "pam.h"
#include "blur.h"

/*
 Blurs image horizontally (width 2*size+1) and writes it transposed to dst (called twice gives 2d blur)
 */
static void transposing_1d_blur(float *restrict src, float *restrict dst, unsigned int width, unsigned int height, const unsigned int size)
{
    const float sizef = size;

    for(unsigned int j=0; j < height; j++) {
        float *restrict row = src + j*width;

        // accumulate sum for pixels outside line
        float sum;
        sum = row[0]*sizef;
        for(unsigned int i=0; i < size; i++) {
            sum += row[i];
        }

        // blur with left side outside line
        for(unsigned int i=0; i < size; i++) {
            sum -= row[0];
            sum += row[i+size];

            dst[i*height + j] = sum / (sizef*2.f);
        }

        for(unsigned int i=size; i < width-size; i++) {
            sum -= row[i-size];
            sum += row[i+size];

            dst[i*height + j] = sum / (sizef*2.f);
        }

        // blur with right side outside line
        for(unsigned int i=width-size; i < width; i++) {
            sum -= row[i-size];
            sum += row[width-1];

            dst[i*height + j] = sum/(sizef*2.0f);
        }
    }
}

/**
 * Picks maximum of neighboring pixels (blur + lighten)
 */
void max3(float *src, float *dst, unsigned int width, unsigned int height)
{
    for(unsigned int j=0; j < height; j++) {
        const float *row = src + j*width,
        *prevrow = src + (j > 1 ? j-1 : 0)*width,
        *nextrow = src + MIN(height-1,j+1)*width;

        float prev,curr=row[0],next=row[0];

        for(unsigned int i=0; i < width-1; i++) {
            prev=curr;
            curr=next;
            next=row[i+1];

            float t1 = MAX(prev,next);
            float t2 = MAX(nextrow[i],prevrow[i]);
            *dst++ = MAX(curr,MAX(t1,t2));
        }
        float t1 = MAX(curr,next);
        float t2 = MAX(nextrow[width-1],prevrow[width-1]);
        *dst++ = MAX(t1,t2);
    }
}

/**
 * Picks minimum of neighboring pixels (blur + darken)
 */
void min3(float *src, float *dst, unsigned int width, unsigned int height)
{
    for(unsigned int j=0; j < height; j++) {
        const float *row = src + j*width,
        *prevrow = src + (j > 1 ? j-1 : 0)*width,
        *nextrow = src + MIN(height-1,j+1)*width;

        float prev,curr=row[0],next=row[0];

        for(unsigned int i=0; i < width-1; i++) {
            prev=curr;
            curr=next;
            next=row[i+1];

            float t1 = MIN(prev,next);
            float t2 = MIN(nextrow[i],prevrow[i]);
            *dst++ = MIN(curr,MIN(t1,t2));
        }
        float t1 = MIN(curr,next);
        float t2 = MIN(nextrow[width-1],prevrow[width-1]);
        *dst++ = MIN(t1,t2);
    }
}

/*
 Filters src image and saves it to dst, overwriting tmp in the process.
 Image must be width*height pixels high. Size controls radius of box blur.
 */
void blur(float *src, float *tmp, float *dst, unsigned int width, unsigned int height, unsigned int size)
{
    if (width < 2*size+1 || height < 2*size+1) return;
    transposing_1d_blur(src, tmp, width, height, size);
    transposing_1d_blur(tmp, dst, height, width, size);
}
