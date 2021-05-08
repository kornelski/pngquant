/*
** © 2019 by Kornel Lesiński.
**
** See COPYRIGHT file for license.
*/

#[cfg(feature = "openmp")]
extern crate openmp_sys;

extern crate imagequant_sys;
extern crate libpng_sys;

#[cfg(feature = "cocoa")]
pub mod rwpng_cocoa;

#[cfg(feature = "lcms2")]
extern crate lcms2_sys;

use imagequant_sys::liq_error::LIQ_OK;
use imagequant_sys::*;
use libc::FILE;
use crate::ffi::PNGQUANT_VERSION;
use crate::ffi::pngquant_internal_print_config;
use std::os::raw::{c_uint, c_char, c_void};

use std::ptr;
use std::io;
use std::ffi::{CString, CStr};

mod ffi;
use crate::ffi::*;
use crate::ffi::pngquant_error::*;

fn unwrap_ptr(opt: Option<&CString>) -> *const c_char {
    opt.map(|c| c.as_ptr()).unwrap_or(ptr::null())
}

fn print_full_version(fd: &mut dyn io::Write, c_fd: *mut FILE) {
    let _ = writeln!(fd, "pngquant, {} (Rust), by Kornel Lesinski, Greg Roelofs.", unsafe{CStr::from_ptr(PNGQUANT_VERSION)}.to_str().unwrap());
    let _ = fd.flush();
    unsafe{pngquant_internal_print_config(c_fd);}
    let _ = writeln!(fd);
}

fn print_usage(fd: &mut dyn io::Write) {
    let _ = writeln!(fd, "{}", unsafe { CStr::from_ptr(PNGQUANT_USAGE) }.to_str().unwrap());
}

/**
 *   N = automatic quality, uses limit unless force is set (N-N or 0-N)
 *  -N = no better than N (same as 0-N)
 * N-M = no worse than N, no better than M
 * N-  = no worse than N, perfect if possible (same as N-100)
 *
 * where N,M are numbers between 0 (lousy) and 100 (perfect)
 */
fn parse_quality(quality: &str) -> Option<(u8, u8)> {
    let mut parts = quality.splitn(2, '-');
    let left = parts.next().unwrap();
    let right = parts.next();

    Some(match (left, right) {
        // quality="%d-"
        (t, Some("")) => {
            (t.parse().ok()?, 100)
        },
        // quality="-%d"
        ("", Some(t)) => {
            (0, t.parse().ok()?)
        },
        // quality="%d"
        (t, None) => {
            let target = t.parse().ok()?;
            (((target as u16)*9/10) as u8, target)
        },
        // quality="%d-%d"
        (l, Some(t)) => {
            (l.parse().ok()?, t.parse().ok()?)
        },
    })
}

unsafe extern "C" fn log_callback(_a: &liq_attr, msg: *const c_char, _user: *mut c_void) {
    eprintln!("{}", CStr::from_ptr(msg).to_str().unwrap());
}

fn main() {
    std::process::exit(run() as _);
}

fn run() -> ffi::pngquant_error {
    let mut opts = getopts::Options::new();

    opts.optflag("v", "verbose", "");
    opts.optflag("h", "help", "");
    opts.optflag("q", "quiet", "");
    opts.optflag("f", "force", "");
    opts.optflag("", "no-force", "");
    opts.optflag("", "ordered", "");
    opts.optflag("", "nofs", "");
    opts.optflag("", "iebug", "");
    opts.optflag("", "transbug", "");
    opts.optflag("", "skip-if-larger", "");
    opts.optflag("", "strip", "");
    opts.optflag("V", "version", "");
    opts.optflagopt("", "floyd", "0.0-1.0", "");
    opts.optopt("", "ext", "extension", "");
    opts.optopt("o", "output", "file", "");
    opts.optopt("s", "speed", "4", "");
    opts.optopt("Q", "quality", "0-100", "");
    opts.optopt("", "posterize", "0", "");
    opts.optopt("", "map", "png", "");

    let args: Vec<_> = wild::args().skip(1).collect();
    let has_some_explicit_args = !args.is_empty();
    let mut m = match opts.parse(args) {
        Ok(m) => m,
        Err(err) => {
            eprintln!("{}", err);
            print_usage(&mut io::stderr());
            return MISSING_ARGUMENT;
        },
    };

    let posterize = m.opt_str("posterize").and_then(|p| p.parse().ok()).unwrap_or(0);
    let floyd = m.opt_str("floyd").and_then(|p| p.parse().ok()).unwrap_or(1.);

    let quality = m.opt_str("quality");
    let extension = m.opt_str("ext").and_then(|s| CString::new(s).ok());
    let map_file = m.opt_str("map").and_then(|s| CString::new(s).ok());

    let colors = if let Some(c) = m.free.get(0).and_then(|s| s.parse().ok()) {
        m.free.remove(0);
        if m.free.len() == 0 {
            m.free.push("-".to_owned()); // stdin default
        }
        c
    } else {0};
    let using_stdin = m.free.len() == 1 && Some("-") == m.free.get(0).map(|s| s.as_str());
    let mut using_stdout = using_stdin;
    let output_file_path = m.opt_str("o").and_then(|s| {
        if s == "-" {
            using_stdout = true;
            None
        } else {
            using_stdout = false;
            CString::new(s).ok()
        }
    });

    let files: Vec<_> = m.free.drain(..).filter_map(|s| CString::new(s).ok()).collect();
    let file_ptrs: Vec<_> = files.iter().map(|s| s.as_ptr()).collect();

    let mut options = pngquant_options {
        quality: ptr::null_mut(), // handled in Rust now
        extension: unwrap_ptr(extension.as_ref()),
        output_file_path: unwrap_ptr(output_file_path.as_ref()),
        map_file: unwrap_ptr(map_file.as_ref()),
        files: file_ptrs.as_ptr(),
        num_files: file_ptrs.len() as c_uint,
        using_stdin,
        using_stdout,
        missing_arguments: !has_some_explicit_args,
        colors,
        speed: 0, // handled in Rust
        posterize,
        floyd,
        force: m.opt_present("force") && !m.opt_present("no-force"),
        skip_if_larger: m.opt_present("skip-if-larger"),
        strip: m.opt_present("strip"),
        iebug: false,
        last_index_transparent: false, // handled in Rust
        print_help: m.opt_present("h"),
        print_version: m.opt_present("V"),
        verbose: m.opt_present("v"),

        fixed_palette_image: ptr::null_mut(),
        log_callback: None,
        log_callback_user_info: ptr::null_mut(),
        fast_compression: false,
        min_quality_limit: false,
    };

    if m.opt_present("nofs") || m.opt_present("ordered") {
        options.floyd = 0.;
    }

    if options.print_version {
        println!("{}", unsafe { CStr::from_ptr(PNGQUANT_VERSION) }.to_str().unwrap());
        return SUCCESS;
    }

    if options.missing_arguments {
        print_full_version(&mut io::stderr(), unsafe { pngquant_c_stderr() });
        print_usage(&mut io::stderr());
        return MISSING_ARGUMENT;
    }

    if options.print_help {
        print_full_version(&mut io::stdout(), unsafe { pngquant_c_stdout() });
        print_usage(&mut io::stdout());
        return SUCCESS;
    }

    let liq = unsafe { liq_attr_create().as_mut().unwrap() };

    if options.verbose {
        unsafe{liq_set_log_callback(liq, Some(log_callback), ptr::null_mut());}
        options.log_callback = Some(log_callback);
    }

    if m.opt_present("transbug") {
        unsafe{liq_set_last_index_transparent(liq, true as _);}
    }

    if let Some(speed) = m.opt_str("speed") {
        let set_ok = speed.parse().ok()
            .filter(|&s: &u8| s>=1 && s <=11)
            .map_or(false, |mut speed| {
                if speed >= 10 {
                    options.fast_compression = true;
                    if speed == 11 {
                        speed = 10;
                        options.floyd = 0.0;
                    }
                }
                LIQ_OK == unsafe{liq_set_speed(liq, speed.into())}
            });
        if !set_ok {
            eprintln!("Speed should be between 1 (slow) and 11 (fast).");
            return INVALID_ARGUMENT;
        }
    }

    if let Some(q) = quality.as_ref() {
        if let Some((limit, target)) = parse_quality(q) {
            options.min_quality_limit = limit > 0;
            if LIQ_OK != unsafe { liq_set_quality(liq, limit.into(), target.into()) } {
                eprintln!("Quality value(s) must be numbers in range 0-100.");
                return INVALID_ARGUMENT;
            }
        } else {
            eprintln!("Quality should be in format min-max where min and max are numbers in range 0-100.");
            return INVALID_ARGUMENT;
        }
    }

    if options.colors > 0 && LIQ_OK != unsafe { liq_set_max_colors(liq, options.colors as _) } {
        eprintln!("Number of colors must be between 2 and 256.");
        return INVALID_ARGUMENT;
    }

    if options.posterize > 0 && LIQ_OK != unsafe { liq_set_min_posterization(liq, options.posterize as _) } {
        eprintln!("Posterization should be number of bits in range 0-4.");
        return INVALID_ARGUMENT;
    }

    if !options.extension.is_null() && !options.output_file_path.is_null() {
        eprintln!("--ext and --output options can't be used at the same time\n");
        return INVALID_ARGUMENT;
    }

    // new filename extension depends on options used. Typically basename-fs8.png
    if options.extension.is_null() {
        options.extension = if options.floyd > 0. { b"-fs8.png\0" } else { b"-or8.png\0" }.as_ptr() as *const _;
    }

    if !options.output_file_path.is_null() && options.num_files != 1 {
        eprintln!("  error: Only one input file is allowed when --output is used. This error also happens when filenames with spaces are not in quotes.");
        return INVALID_ARGUMENT;
    }

    if options.using_stdout && !options.using_stdin && options.num_files != 1 {
        eprintln!("  error: Only one input file is allowed when using the special output path \"-\" to write to stdout. This error also happens when filenames with spaces are not in quotes.");
        return INVALID_ARGUMENT;
    }

    if options.num_files == 0 && !options.using_stdin {
        eprintln!("No input files specified.");
        if options.verbose {
            print_full_version(&mut io::stdout(), unsafe { pngquant_c_stdout() });
        }
        print_usage(&mut io::stderr());
        return MISSING_ARGUMENT;
    }

    let retval = unsafe {pngquant_main_internal(&mut options, liq)};
    unsafe {liq_attr_destroy(liq);}
    retval
}
