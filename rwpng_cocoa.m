//
//  rwpng_cocoa.m
//  pngquant
//

#include "rwpng.h"

#ifdef USE_COCOA

#import <Cocoa/Cocoa.h>
#import <CoreGraphics/CoreGraphics.h>
#include <stdio.h>

int rwpng_read_image24_cocoa(FILE *fp, png24_image *out)
{
    unsigned char *data_rgba;
    CGFloat width, height;
    @autoreleasepool {
        NSFileHandle *fh = [[NSFileHandle alloc] initWithFileDescriptor:fileno(fp)];
        CGImageRef image = [[NSBitmapImageRep imageRepWithData:[fh readDataToEndOfFile]] CGImage];
        [fh release];

        if (!image) return READ_ERROR;

        width = CGImageGetWidth(image);
        height = CGImageGetHeight(image);

        data_rgba = malloc(width*height*4);

        CGColorSpaceRef colorspace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);

        CGContextRef context = CGBitmapContextCreate(data_rgba,
                                                     width, height,
                                                     8, width*4,
                                                     colorspace,
                                                     kCGImageAlphaPremultipliedLast);

        if (!context) return READ_ERROR;

        CGContextDrawImage(context, CGRectMake(0.0, 0.0, width, height), image);
        CGContextRelease(context);
        CGColorSpaceRelease(colorspace);
    }
    // reverse premultiplication
    for(int i=0; i < width*height*4; i+=4) {
        if (data_rgba[i+3]) {
            data_rgba[i+0] = data_rgba[i+0]*255/data_rgba[i+3];
            data_rgba[i+1] = data_rgba[i+1]*255/data_rgba[i+3];
            data_rgba[i+2] = data_rgba[i+2]*255/data_rgba[i+3];
        }
    }

    out->gamma = 0.45455;
    out->width = width;
    out->height = height;
    out->rgba_data = data_rgba;
    out->row_pointers = malloc(sizeof(out->row_pointers[0])*out->height);
    for(int i=0; i < out->height; i++) {
        out->row_pointers[i] = out->rgba_data + (int)width*4*i;
    }
    return SUCCESS;
}

#endif
