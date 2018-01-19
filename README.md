# pngquant 2 [![build](https://travis-ci.org/kornelski/pngquant.svg?branch=master)](https://travis-ci.org/kornelski/pngquant)

[pngquant](https://pngquant.org) is a PNG compresor that significantly reduces file sizes by converting images to a more efficient 8-bit PNG format *with alpha channel* (often 60-80% smaller than 24/32-bit PNG files). Compressed images are fully standards-compliant and are supported by all web browsers and operating systems.

[This](https://github.com/kornelski/pngquant) is the official `pngquant` repository. The compression engine is also available [as an embeddable library](https://github.com/ImageOptim/libimagequant).

## Usage

- batch conversion of multiple files: `pngquant *.png`
- Unix-style stdin/stdout chaining: `… | pngquant - | …`

To further reduce file size, try [optipng](http://optipng.sourceforge.net), [ImageOptim](https://imageoptim.com), or [zopflipng](https://github.com/google/zopfli).

## Features

 * High-quality palette generation
  - advanced quantization algorithm with support for gamma correction and premultiplied alpha
  - unique dithering algorithm that does not add unnecessary noise to the image

 * Configurable quality level
  - automatically finds required number of colors and can skip images which can't be converted with the desired quality

 * Fast, modern code
  - based on a portable [libimagequant library](https://github.com/ImageOptim/libimagequant)
  - C99 with no workarounds for legacy systems or compilers ([apart from Visual Studio](https://github.com/kornelski/pngquant/tree/msvc))
  - multicore support (via OpenMP) and Intel SSE optimizations

## Options

See `pngquant -h` for full list.

### `--quality min-max`

`min` and `max` are numbers in range 0 (worst) to 100 (perfect), similar to JPEG. pngquant will use the least amount of colors required to meet or exceed the `max` quality. If conversion results in quality below the `min` quality the image won't be saved (if outputting to stdin, 24-bit original will be output) and pngquant will exit with status code 99.

    pngquant --quality=65-80 image.png

### `--ext new.png`

Set custom extension (suffix) for output filename. By default `-or8.png` or `-fs8.png` is used. If you use `--ext=.png --force` options pngquant will overwrite input files in place (use with caution).

### `-o out.png` or `--output out.png`

Writes converted file to the given path. When this option is used only single input file is allowed.

### `--skip-if-larger`

Don't write converted files if the conversion isn't worth it.

### `--speed N`

Speed/quality trade-off from 1 (slowest, highest quality, smallest files) to 11 (fastest, less consistent quality, light comperssion). The default is 3. It's recommended to keep the default, unless you need to generate images in real time (e.g. map tiles). Higher speeds are fine with 256 colors, but don't handle lower number of colors well.

### `--nofs`

Disables Floyd-Steinberg dithering.

### `--floyd=0.5`

Controls level of dithering (0 = none, 1 = full). Note that the `=` character is required.

### `--posterize bits`

Reduce precision of the palette by number of bits. Use when the image will be displayed on low-depth screens (e.g. 16-bit displays or compressed textures in ARGB444 format).

### `--strip`

Don't copy optional PNG chunks. Metadata is always removed on Mac (when using Cocoa reader).

See [man page](https://github.com/kornelski/pngquant/blob/master/pngquant.1) (`man pngquant`) for the full list of options.

## License

pngquant is dual-licensed:

* Under **GPL v3** or later with an additional [copyright notice](https://github.com/kornelski/pngquant/blob/master/COPYRIGHT) that must be kept for the older parts of the code.

* Or [a **commercial license**](https://supportedsource.org/projects/pngquant) for use in non-GPL software (e.g. closed-source or App Store distribution). You can [get the license via Supported Source](https://supportedsource.org/projects/pngquant/purchase). Email kornel@pngquant.org if you have any questions.
