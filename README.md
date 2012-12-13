#pngquant

This is the official new `pngquant`.

[pngquant](http://pngquant.org) converts 24/32-bit RGBA PNGs to 8-bit palette with *alpha channel preserved*.
Such images are compatible with all modern browsers, and a special compatibility setting exists which helps transparency degrade well in Internet Explorer 6.

Quantized files are often 40-70% smaller than their 24/32-bit version.

This utility works on Linux, Mac OS X and Windows.

##Usage

- batch conversion of multiple files: `pngquant 256 *.png`
- Unix-style stdin/stdout chaining: `… | pngquant 16 | …`

To further reduce file size, you may want to consider [optipng](http://optipng.sourceforge.net) or [ImageOptim](http://imageoptim.pornel.net).


##Improvements since 1.0

* Significantly better quality of quantisation

  - more accurate remapping of semitransparent colors
  - special dithering algorithm that does not add noise in well-quantized areas of the image
  - uses variance instead of popularity for box selection (improvement suggested in the original median cut paper)
  - feedback loop that repeats median cut for poorly quantized colors
  - additional colormap improvement using Voronoi iteration
  - supports much larger number of colors in input images without degradation of quality
  - gamma correction (output is always generated with gamma 2.2 for web compatibility)

* More flexible commandline usage

  - number of colors defaults to 256
  - long options and standard switches like `--` and `-` are allowed

* Refactored and modernised code

  - C99 with no workarounds for old systems
  - floating-point math used throughout
  - Intel SSE3 optimisations
  - multicore support via OpenMP

##Options

See `pngquant -h` for full list.

###`--quality min-max`

`min` and `max` are numbers in range 0 (worst) to 100 (perfect), similar to JPEG. pngquant will use the least amount of colors required to meet or exceed the `max` quality. If conversion results in quality below the `min` quality the image won't be saved (if outputting to stdin, 24-bit original will be output) and pngquant will exit with status code 99.

    pngquant --quality=65-80 image.png

###`--ext new.png`

Set custom extension (suffix) for output filename. By default `-or8.png` or `-fs8.png` is used. If you use `-ext .png -force` options pngquant will overwrite input files in place (use with caution).

###`--speed N`

Speed/quality trade-off from 1 (brute-force) to 10 (fastest). The default is 3. Speed 10 has 5% lower quality, but is 8 times faster than the default.

###`--iebug`

Workaround for IE6, which only displays fully opaque pixels. pngquant will make almost-opaque pixels fully opaque and will avoid creating new transparent colors.

###`--version`

Print version information to stdout.

###`-`

Read image from stdin and send result to stdout.

###`--`

Stops processing of arguments. This allows use of file names that start with `-`. If you're using pngquant in a script, it's advisable to put this before file names:

    pngquant $OPTIONS -- "$FILE"

