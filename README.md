#PNGquant-improved

This is a [fork](http://pornel.net/pngquant) of [pngquant](http://www.libpng.org/pub/png/apps/pngquant.html).

##Improvements

* Significantly better quality of quantisation

  - uses variance instead of popularity for box selection (improvement suggested in the original median cut paper)
  - supports much larger number of colors in input images without degradation of quality
  - more accurate remapping of transparent colors
  - feedback loop that corrects poorly quantized colors
  - gamma correction and floating-point math used throughout

* More flexible commandline usage

  - number of colors defaults to 256
  - standard switches like `--` and `-` are allowed

* Modernised code

  - C99
  - Intel SSE3 optimisations

##Aboug PNGquant

Typically, pngquant is used to convert 32-bit RGBA PNGs to 8-bit RGBA-palette
PNGs in order to save file space. For example, for the web.

This utility works on Linux and UNIX systems (including Mac OS X) and should
work on modern Windows platforms.

Pngquant provides the following features:

- reduction of all PNG image types to a palette with 256 colors or less
- diffusion (Floyd-Steinberg)
- automatic optimization of tRNS chunks
- batch conversion of multiple files, e.g.: `pngquant 256 *.png`
- Unix-style stdin/stdout chaining, e.g.: `… | pngquant 16 | …`

These features are currently lacking:

- no ancillary chunk preservation
- no preservation of significant-bits info after rescaling (sBIT chunk)
- no mapfile support
- no "native" handling of 16-bit-per-sample files or gray+alpha files
  (i.e. all samples are truncated to 8 bits and all images are promoted
  to RGBA before quantization)

If the goal is to reduce file size, then be sure to check the file size before
and after quantization. Although palette-based images are typically much smaller
than RGBA-images, they don't compress nearly as well as grayscale and truecolor
images.

To further reduce file size, you may want to consider the following tools:

- pngcrush - http://pmt.sourceforge.net/pngcrush/
- optipng  - http://optipng.sourceforge.net/

For copyright and license information, check out the COPYRIGHT file. For the
change log, see the CHANGELOG file.

The INSTALL file explains how to build pngquant from source.

The homepage of original pngquant is:
http://www.libpng.org/pub/png/apps/pngquant.html

##Options

See `pngquant -h` for full list.

###`-ext new.png`

Set custom extension for output filename. By default `-or8.png` or `-fs8.png` is used.

###`-speed N`

Speed/quality trade-off. The default is 3 and lower settings rarely increase quality, but are much slower.

Speeds 1-6 use feedback loop mechanism (quantize image, find worst colors, quantize again with more weight on that colors).
Speeds 8-10 reduce input depth from 8 bit per gun to 7 bit (posterize to 128 levels) or less, if image has *a lot* of distinct color

###`-iebug`

Workaround for IE6, which only displays fully opaque pixels. PNGquant will make almost-opaque pixels fully opaque and will avoid creating new transparent colors.

###`-version`

Print version information to stdout.

###`-`

Read image from stdin and send result to stdout.

###`--`

Stops processing of arguments. This allows use of file names that start with `-`. If you're using pngquant in a script, it's advisable to put this before file names:

    pngquant $OPTIONS -- "$FILE"

