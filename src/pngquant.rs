//! © 2021 by Kornel Lesiński.
//!
//! See COPYRIGHT file for license.

use load_image::ImageData;
use load_image::export::rgb::{ComponentMap};
use clap::{crate_version, ValueHint, value_parser, ArgAction};
use clap::{Command, Arg};
use imagequant::{Attributes, Error as liq_error, Image, RGBA};
use rayon::prelude::{IntoParallelRefIterator, ParallelIterator};
use std::borrow::Cow;
use std::ffi::{OsStr, OsString};
use std::fs::Metadata;
use std::io::{self, Read, Write};
use std::mem::MaybeUninit;
use std::path::{Path, PathBuf};

/// Back compat with pngquant2
enum ExitCode {
    MissingArgument = 1,
    ReadError = 2,
    InvalidArgument = 4,
    NotOverwritingError = 15,
    WriteError = 16,
    OutOfMemory = 17,
    Unsupported = 18, // Missing SSE
    EncodingError = 25,
    WrongInputColorType = 26,
    InternalError = 35,
    QualityTooLow = 99,
}

/**
 *   N = automatic quality, uses limit unless force is set (N-N or 0-N)
 *  -N = no better than N (same as 0-N)
 * N-M = no worse than N, no better than M
 * N-  = no worse than N, perfect if possible (same as N-100)
 *
 * where N,M are numbers between 0 (lousy) and 100 (perfect)
 */
fn parse_quality(quality: &str) -> Result<(u8, u8), String> {
    let mut parts = quality.splitn(2, '-');
    let left = parts.next().unwrap();
    let right = parts.next();

    Ok(match (left, right) {
        // quality="%d-"
        (t, Some("")) => (t.parse().map_err(|_| "First number is invalid")?, 100),
        // quality="-%d"
        ("", Some(t)) => (0, t.parse().map_err(|_| "Last number is invalid")?),
        // quality="%d"
        (t, None) => {
            let target = t.parse().map_err(|_| "Quality value is not a number")?;
            (((target as u16) * 9 / 10) as u8, target)
        }
        // quality="%d-%d"
        (l, Some(t)) => (l.parse().map_err(|_| "First number is invalid")?, t.parse().map_err(|_| "Last number is invalid")?),
    })
}

enum Input<'a> {
    Paths(Vec<&'a Path>),
    Stdin(Vec<u8>),
}

enum Output<'a> {
    Path(&'a Path),
    Stdout,
    FileWithExtension(&'a OsStr),
}

// unsafe extern "C" fn log_callback(_a: &liq_attr, msg: *const c_char, _user: *mut c_void) {
//     eprintln!("{}", CStr::from_ptr(msg).to_str().unwrap());
// }

struct Error {
    pub code: ExitCode,
    pub msg: Cow<'static, str>,
}

impl Error {
    #[inline(always)]
    pub fn new(msg: impl Into<Cow<'static, str>>, code: impl Into<ExitCode>) -> Self {
        Self {
            code: code.into(),
            msg: msg.into(),
        }
    }
}

impl From<liq_error> for ExitCode {
    fn from(e: liq_error) -> Self {
        use liq_error::*;
        match e {
            QualityTooLow => Self::QualityTooLow,
            ValueOutOfRange => Self::InvalidArgument,
            OutOfMemory => Self::OutOfMemory,
            Aborted => Self::NotOverwritingError,
            InternalError => Self::InternalError,
            BufferTooSmall => Self::WrongInputColorType,
            InvalidPointer => Self::InvalidArgument,
            Unsupported => Self::Unsupported,
            _ => Self::InvalidArgument,
        }
    }
}

trait LiqErrorExt<T> {
    fn msg(self, msg: impl Into<Cow<'static, str>>) -> Result<T, Error>;
}

impl<T, E: Into<ExitCode>> LiqErrorExt<T> for Result<T, E> {
    #[inline(always)]
    fn msg(self, msg: impl Into<Cow<'static, str>>) -> Result<T, Error> {
        match self {
            Ok(v) => Ok(v),
            Err(e) => Err(Error::new(msg, e)),
        }
    }
}

fn main() {
    if let Err(e) = run() {
        eprintln!("error: {}", e.msg);
        std::process::exit(e.code as i32);
    }
}

fn parse_floyd(v: &str) -> Result<f32, String> {
    let val = v.parse::<f32>().map_err(|e| e.to_string())?;
    if val < 0. || val > 1. {
        return Err("Must be between 0.0 and 1.0".into());
    }
    Ok(val)
}

fn parse_speed(v: &str) -> Result<u8, String> {
    let speed = v.parse::<u8>().map_err(|e| e.to_string())?;
    if speed == 0 || speed > 11 {
        return Err("Speed should be between 1 (slow) and 11 (fast)".into());
    }
    Ok(speed)
}

fn run() -> Result<(), Error> {
    use ExitCode::*;

    let mut args = Command::new("pngquant")
        .version(crate_version!())
        .author("Kornel Lesiński")
        .help_template("{bin}, {version}, by {author}, https://pngquant.org\n\nusage:  {usage}\n\noptions:\n{options}\n{positionals}{after-help}")
        .about("Convert true-color PNG images to a smaller PNG8 format. By Kornel Lesiński. https://pngquant.org")
        .after_help("Quantizes one or more 32-bit RGBA PNGs to 8-bit (or smaller) RGBA-palette. \
            The output filename is the same as the input name except that it ends in \"-fs8.png\", \"-or8.png\" or your custom extension \
            (unless the input is stdin, in which case the quantized image will go to stdout). If you pass the special output path \"-\" \
            and a single input file, that file will be processed and the quantized image will go to stdout. \
            The default behavior if the output file exists is to skip the conversion; use --force to overwrite.")
        .arg(Arg::new("verbose")
            .long("verbose")
            .short('v')
            .help("Print status messages")
            .action(ArgAction::SetTrue))
        .arg(Arg::new("force")
            .long("force")
            .short('f')
            .help("Overwrite existing output files")
            .action(ArgAction::SetTrue))
        .arg(Arg::new("ext")
            .long("ext")
            .value_name("-fs8.png")
            .value_parser(value_parser!(PathBuf))
            .help("Set custom suffix/extension for output filenames"))
        .arg(Arg::new("output")
            .long("output")
            .short('o')
            .conflicts_with("ext")
            .value_parser(value_parser!(PathBuf))
            .value_hint(ValueHint::FilePath)
            .help("Write to this file instead ('-' for stdout)"))
        .arg(Arg::new("skip-if-larger")
            .long("skip-if-larger")
            .help("Only save converted files if they're smaller than original")
            .action(ArgAction::SetTrue))
        .arg(Arg::new("nofs")
            .long("nofs")
            .alias("ordered")
            .conflicts_with("floyd")
            .help("Disable dithering")
            .action(ArgAction::SetTrue))
        .arg(Arg::new("speed")
            .long("speed")
            .short('s')
            .value_name("N")
            .value_parser(parse_speed)
            .default_value("3")
            .help("Speed/quality trade-off. 1=slow, 11=fast & rough"))
        .arg(Arg::new("quality")
            .long("quality")
            .short('Q')
            .value_name("min-max")
            .value_parser(parse_quality)
            .help("don't save below min, use fewer colors below max (0-100)"))
        .arg(Arg::new("strip")
            .long("strip")
            .help("Remove optional metadata")
            .action(ArgAction::SetTrue))
        .arg(Arg::new("floyd")
            .long("floyd")
            .value_name("0.x")
            .default_value("1.0")
            .value_parser(parse_floyd)
            .num_args(0..=1)
            .require_equals(true)
            .help("Floyd-Steinberg dithering strength (0-1)"))
        .arg(Arg::new("posterize")
            .long("posterize")
            .value_name("N")
            .value_parser(value_parser!(u8))
            .help("Output lower-precision color (e.g. for ARGB4444 output)"))
        .arg(Arg::new("colors")
            .hide(true)
            .long("colors")
            .short('N')
            .value_name("256")
            .value_parser(value_parser!(u8))
            .help("Max number of colors to use"))
        .arg(Arg::new("map")
            .long("map")
            .value_name("pal.png")
            .value_parser(value_parser!(PathBuf))
            .help("Use a palette of this file instead"))
        .arg(Arg::new("files")
            .value_name("FILES")
            .value_parser(value_parser!(PathBuf))
            .value_hint(ValueHint::FilePath)
            .help("Paths of PNG images to convert ('-' for stdin)"));
    let m = args.get_matches_mut();

    let mut liq = imagequant::new();

    if let Some(&val) = m.get_one::<u8>("posterize") {
        liq.set_min_posterization(val)
            .msg("Posterization should be number of bits in range 0-4.")?;
    }

    let mut fastest_compression = false;
    if let Some(mut speed) = m.get_one::<u8>("speed").copied() {
        if speed >= 10 {
            fastest_compression = true;
            if speed == 11 {
                speed = 10;
            }
        }
        liq.set_speed(speed.into())
            .msg("Speed should be between 1 (slow) and 11 (fast)")?;
    }

    let dithering_level = if let Some(&floyd) = m.get_one::<f32>("floyd") {
        floyd
    } else if m.get_flag("nofs") {
        0.
    } else if !fastest_compression {
        1.
    } else {
        0.
    };

    let map_file = m.get_one::<PathBuf>("map");
    let verbose = m.get_flag("verbose");

    if verbose {
        liq.set_log_callback(|_a, msg| {
            println!("  {}", msg);
        });
    }

    let mut min_quality_limit = false;

    if let Some(&(limit, target)) = m.get_one::<(u8, u8)>("quality") {
        min_quality_limit = limit > 0;
        liq.set_quality(limit, target)
            .msg("Quality value(s) must be numbers in range 0-100.")?;
    }

    let mut files: Vec<_> = m.get_many::<PathBuf>("files")
        .map(|f| f.map(|p| p.as_path()).collect())
        .unwrap_or_default();

    let colors = m.get_one::<u8>("colors").copied()
        .or_else(|| {
            // legacy fallback
            let first_arg = files.get(0)?;
            let colors = first_arg.to_str()?.parse::<u8>().ok()?;
            if !Path::new(first_arg).exists() {
                eprintln!("use --colors {} instead", colors);
                files.remove(0);
                if files.is_empty() {
                    files.push("-".as_ref()); // stdin default
                }
                Some(colors)
            } else {
                None
            }
        });

    if let Some(colors) = colors {
        liq.set_max_colors(colors.into())
            .msg("Number of colors must be between 2 and 256.")?;
    }

    let input = if files.len() == 1 && files[0] == Path::new("-") {
        let mut data_in = Vec::with_capacity(1<<16);
        std::io::stdin().lock().read_to_end(&mut data_in).map_err(|e| Error::new(format!("error reading stdin: {}", e), ReadError))?;
        Input::Stdin(data_in)
    } else {
        Input::Paths(files)
    };

    let extension = m.get_one::<PathBuf>("ext").map(|s| s.as_os_str());
    if extension.is_some() && matches!(input, Input::Stdin(_)) {
        return Err(Error::new("--ext doesn't make sense for stdout output", InvalidArgument));
    }

    let skip_if_larger = m.get_flag("skip-if-larger");
    let force = m.get_flag("force");

    let output = match m.get_one::<PathBuf>("output") {
        Some(s) if s == Path::new("-") => Output::Stdout,
        Some(s) => Output::Path(Path::new(s)),
        None if matches!(input, Input::Stdin(_)) => Output::Stdout,
        None => Output::FileWithExtension(extension.unwrap_or_else(move || {
            if dithering_level > 0. {"-fs8.png"} else {"-or8.png"}.as_ref()
        })),
    };

    if matches!(input, Input::Paths(ref p) if p.len() != 1) {
        if matches!(output, Output::Path(_)) {
            return Err(Error::new("Only one input file is allowed when --output is used. This error also happens when filenames with spaces are not in quotes.", InvalidArgument));
        }
        if matches!(output, Output::Stdout) {
            return Err(Error::new("Only one input file is allowed when using the special output path \"-\" to write to stdout. This error also happens when filenames with spaces are not in quotes.", InvalidArgument));
        }
    }

    if matches!(input, Input::Paths(ref p) if p.is_empty()) {
        if verbose {
            let _ = args.write_long_help(&mut io::stderr().lock());
        }
        return Err(Error::new("No input files specified", MissingArgument));
    }

    let fixed_palette = map_file.map(|map_file| {
        let (img, _) = read_image_from_path(map_file.as_ref())?;

        // settings like speed and posterize are fine, but not the quality limit
        let mut liq = liq.clone();
        liq.set_quality(0, 100).unwrap();

        let mut img = convert_image(&liq, &img)?;
        let mut res = liq.quantize(&mut img).msg("Internal Error")?;
        Ok(res.palette_vec())
    }).transpose()?;

    match &input {
        Input::Stdin(data) => {
            let img = load_image::load_data(data).map_err(|e| Error::new(format!("unable to parse image: {e}"), ExitCode::ReadError))?;
            let png = pngquant_image(&liq, img, dithering_level, fixed_palette.as_deref())?;
            save_image(&output, None, data, &png, skip_if_larger, force)?;
        },
        Input::Paths(files) => {
            // Don't abort on the first failure
            let results = files.par_iter().map(move |&file| {
                let (img, encoded_data) = read_image_from_path(file.as_ref())?;
                let png = pngquant_image(&liq, img, dithering_level, fixed_palette.as_deref())?;
                save_image(&output, Some(file), &encoded_data, &png, skip_if_larger, force)
            }).collect::<Vec<_>>();
            let failed = results.iter().filter(|r| r.is_err()).count();
            let total = results.len();
            if failed > 0 {
                let e = results.into_iter().find(|r| r.is_err()).unwrap();
                if total > 1 {
                    eprintln!("{failed} out of {total} failed");
                }
                e?;
            }
        }
    }
    Ok(())
}

fn save_image(output: &Output, base_path: Option<&Path>, original_data: &[u8], png: &[u8], skip_if_larger: bool, force: bool) -> Result<(), Error> {
    let tmp;
    let path = match output {
        Output::FileWithExtension(ext) => {
            tmp = path_to_save(ext, base_path.unwrap());
            Some(tmp.as_path())
        },
        Output::Path(path) => Some(*path),
        Output::Stdout => None,
    };
    match path {
        Some(path) => {
            let mut original_data_len = original_data.len();
            if let Ok(stat) = std::fs::metadata(path) {
                let sz = file_size(&stat) as usize;
                if sz < original_data_len {
                    original_data_len = sz;
                }
                if !force {
                    return Err(Error::new(format!("skipped {}, because it already exists. Use --force to overwrite files.", path.display()), ExitCode::NotOverwritingError));
                }
            }
            if !skip_if_larger || png.len() < original_data_len {
                write_to_path(path, &png)?
            } else {
                return Err(Error::new(format!("skipped {}, because new {}B file was larger than original {original_data_len}B", path.display(), png.len()), ExitCode::NotOverwritingError));
            }
        },
        None => {
            let use_original = skip_if_larger && png.len() >= original_data.len();
            let data = if !use_original {
                png
            } else {
                original_data
            };
            std::io::stdout().lock().write_all(&data).map_err(|e| Error::new(format!("Can't write to stdout: {}", e), ExitCode::WriteError))?;
            if use_original {
                return Err(Error::new(format!("kept original, because new {}B file was larger than original {}B", png.len(), original_data.len()), ExitCode::NotOverwritingError));
            }
        },
    }
    Ok(())
}

fn path_to_save(ext: &OsStr, base_path: &Path) -> PathBuf {
    let p = base_path.to_string_lossy(); // maybe osstr will be useful one day…
    let stem = p.rsplitn(2, '.').nth(1).unwrap_or(&p);
    let mut s = OsString::with_capacity(stem.len() + ext.len());
    s.extend([stem.as_ref(), ext]);
    s.into()
}

fn write_to_path(path: &Path, png: &[u8]) -> Result<(), Error> {
    std::fs::write(path, png)
        .map_err(|e| Error::new(format!("Can't write '{}': {e}", path.display()), ExitCode::WriteError))
}

fn pngquant_image(liq: &Attributes, img: load_image::Image, dithering_level: f32, fixed_palette: Option<&[RGBA]>) -> Result<Vec<u8>, Error> {
    let mut img = convert_image(liq, &img)?;
    let width = img.width();
    let height = img.height();
    if let Some(fixed_palette) = fixed_palette {
        for c in fixed_palette {
            img.add_fixed_color(*c).msg("Fixed palette error")?;
        }
    }
    let mut res = liq.quantize(&mut img).msg("Quantization failed")?;
    res.set_dithering_level(dithering_level).msg("Invalid dithering level")?;
    let (pal, img) = res.remapped(&mut img).msg("Remapping failed")?;

    encode_image(&pal, &img, width, height)
}

fn encode_image(palette: &[RGBA], pixels: &[u8], width: usize, height: usize) -> Result<Vec<u8>, Error> {
    let mut enc = lodepng::Encoder::new();
    enc.set_palette(palette)
        .and_then(move |_| enc.encode(pixels, width, height))
        .map_err(|e| Error::new(format!("Encoding error: {}", e), ExitCode::EncodingError))
}

fn read_image_from_path(path: &Path) -> Result<(load_image::Image, Vec<u8>), Error> {
    let data = std::fs::read(path).map_err(|e| {
        let tmp;
        let path = if path.is_relative() {
            tmp = std::env::current_dir().unwrap_or_default().join(path);
            tmp.display()
        } else {
            path.display()
        };
        Error::new(format!("Unable to read file from '{path}': {e}"), ExitCode::ReadError)
    })?;
    let img = load_image::load_data(&data).map_err(|e| {
        Error::new(format!("Unable to decode image '{}': {e}", path.display()), ExitCode::ReadError)
    })?;
    Ok((img, data))
}

fn row_converter<'a, T: Send + Sync + Copy>(data: &'a [T], convert_pixel: impl Fn(T) -> RGBA + Send + Sync + 'static) -> impl Fn(&mut [MaybeUninit<imagequant::RGBA>], usize) + Send + Sync + 'a {
    move |output_row: &mut [MaybeUninit<imagequant::RGBA>], y: usize| {
        let width = output_row.len();
        let input_row = &data[y * width..y * width + width];
        for (dst, src) in output_row.iter_mut().zip(input_row) {
            dst.write(convert_pixel(*src));
        }
    }
}

fn convert_image<'a>(liq: &Attributes, img: &'a load_image::Image) -> Result<Image<'a>, Error> {
    match &img.bitmap {
        ImageData::RGB8(pixels) => unsafe { Image::new_fn(liq, row_converter(pixels, |r| r.alpha(255)), img.width, img.height, 0.) },
        ImageData::RGBA8(pixels) => liq.new_image_borrowed(pixels, img.width, img.height, 0.),
        ImageData::RGB16(pixels) => unsafe { Image::new_fn(liq, row_converter(pixels, |px| px.map(|c| (c>>8) as u8).alpha(255)), img.width, img.height, 0.) },
        ImageData::RGBA16(pixels) => unsafe { Image::new_fn(liq, row_converter(pixels, |px| px.map(|c| (c>>8) as u8)), img.width, img.height, 0.) },
        ImageData::GRAY8(pixels) => unsafe { Image::new_fn(liq, row_converter(pixels, |g| RGBA::new(g.0,g.0,g.0,255)), img.width, img.height, 0.) },
        ImageData::GRAY16(pixels) => unsafe { Image::new_fn(liq, row_converter(pixels, |g| {let g = (g.0>>8) as u8; RGBA::new(g,g,g,255)}), img.width, img.height, 0.) },
        ImageData::GRAYA8(pixels) => unsafe { Image::new_fn(liq, row_converter(pixels, |g| RGBA::new(g.0,g.0,g.0,g.1)), img.width, img.height, 0.) },
        ImageData::GRAYA16(pixels) => unsafe { Image::new_fn(liq, row_converter(pixels, |ga| {let g = (ga.0>>8) as u8; RGBA::new(g,g,g,(ga.1>>8) as u8)}), img.width, img.height, 0.) },
    }.map_err(|e| {
        Error::new(format!("internal error: {}", e), ExitCode::WrongInputColorType)
    })
}

#[cfg(windows)]
fn file_size(metadata: &Metadata) -> u64 {
    use std::os::windows::fs::MetadataExt;
    metadata.file_size()
}

#[cfg(unix)]
fn file_size(metadata: &Metadata) -> u64 {
    use std::os::unix::fs::MetadataExt;
    metadata.size()
}
