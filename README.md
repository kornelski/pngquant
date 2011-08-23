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
