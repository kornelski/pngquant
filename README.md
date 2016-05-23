# pngquant 2

[This](https://github.com/pornel/pngquant) is the official `pngquant` and `libimagequant`.

[pngquant](https://pngquant.org) converts 24/32-bit RGBA PNGs to 8-bit palette with *alpha channel preserved*.
Such images are fully standards-compliant and are supported by all web browsers.

Quantized files are often 60-80% smaller than their 24/32-bit versions.

This utility works on Linux, Mac OS X and Windows.

## Usage

- batch conversion of multiple files: `pngquant *.png`
- Unix-style stdin/stdout chaining: `… | pngquant - | …`

To further reduce file size, try [optipng](http://optipng.sourceforge.net) or [ImageOptim](https://imageoptim.com).

## Improvements since 1.0

Generated files are both smaller and look much better.

* Significantly better quality of quantisation

  - more accurate remapping of semitransparent colors
  - special dithering algorithm that does not add noise in well-quantized areas of the image
  - uses variance instead of popularity for box selection (improvement suggested in the original median cut paper)
  - feedback loop that repeats median cut for poorly quantized colors
  - additional colormap improvement using Voronoi iteration
  - supports much larger number of colors in input images without degradation of quality
  - gamma correction and optional color profile support (output is always in gamma 2.2 for web compatibility)

* More flexible commandline usage

  - number of colors defaults to 256, and can be set automatically with the `--quality` switch
  - long options and standard switches like `--` and `-` are allowed

* Refactored and modernised code

  - C99 with no workarounds for legacy systems or compilers ([apart from Visual Studio](https://github.com/pornel/pngquant/tree/msvc))
  - floating-point math used throughout
  - Intel SSE optimisations
  - multicore support via OpenMP
  - quantization moved to standalone libimagequant

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

Speed/quality trade-off from 1 (brute-force) to 11 (fastest). The default is 3. Speed 10 has 5% lower quality, but is 8 times faster than the default. Speed 11 disables dithering and lowers compression level.

### `--nofs`

Disables Floyd-Steinberg dithering.

### `--floyd=0.5`

Controls level of dithering (0 = none, 1 = full). Note that the `=` character is required.

### `--posterize bits`

Reduce precision of the palette by number of bits. Use when the image will be displayed on low-depth screens (e.g. 16-bit displays or compressed textures in ARGB444 format).

### `--version`

Print version information to stdout.

### `-`

Read image from stdin and send result to stdout.

### `--`

Stops processing of arguments. This allows use of file names that start with `-`. If you're using pngquant in a script, it's advisable to put this before file names:

    pngquant $OPTIONS -- "$FILE"

## License

pngquant is dual-licensed:

* GPL v3 or later, and additional copyright notice must be kept for older parts of the code. See [COPYRIGHT](https://github.com/pornel/pngquant/blob/master/COPYRIGHT) for details.

* For use in non-GPL software (e.g. closed-source or App Store distribution) please ask kornel@pngquant.org for a commercial license.
