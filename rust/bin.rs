#![cfg_attr(feature="alloc_system", feature(alloc_system))]

#[cfg(feature="alloc_system")]
extern crate alloc_system;
extern crate imagequant_sys;
extern crate libpng_sys;
extern crate getopts;

#[cfg(feature = "lcms2")]
extern crate lcms2_sys;

use std::os::raw::{c_uint, c_char};
use std::io;
use std::io::Write;
use std::process;
use std::env;
use std::ptr;
use std::ffi::CString;

mod ffi;
use ffi::*;

fn unwrap_ptr(opt: Option<&CString>) -> *const c_char {
    opt.map(|c| c.as_ptr()).unwrap_or(ptr::null())
}

fn main() {
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
    opts.optflag("", "floyd", ""); // https://github.com/rust-lang-nursery/getopts/issues/49
    opts.optopt("", "ext", "extension", "");
    opts.optopt("o", "output", "file", "");
    opts.optopt("s", "speed", "3", "");
    opts.optopt("Q", "quality", "0-100", "");
    opts.optopt("", "posterize", "0", "");
    opts.optopt("", "map", "png", "");

    let has_some_explicit_args = env::args().skip(1).next().is_some();
    let mut m = match opts.parse(env::args().skip(1)) {
        Ok(m) => m,
        Err(err) => {
            writeln!(&mut io::stderr(), "{}", err).ok();
            process::exit(2);
        },
    };

    let posterize = m.opt_str("posterize").and_then(|p| p.parse().ok()).unwrap_or(0);
    let speed = m.opt_str("speed").and_then(|p| p.parse().ok()).unwrap_or(0);
    let floyd = m.opt_str("floyd").and_then(|p| p.parse().ok()).unwrap_or(1.);

    let quality = m.opt_str("quality").and_then(|s| CString::new(s).ok());
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
        quality: unwrap_ptr(quality.as_ref()),
        extension: unwrap_ptr(extension.as_ref()),
        output_file_path: unwrap_ptr(output_file_path.as_ref()),
        map_file: unwrap_ptr(map_file.as_ref()),
        files: file_ptrs.as_ptr(),
        num_files: file_ptrs.len() as c_uint,
        using_stdin,
        using_stdout,
        missing_arguments: !has_some_explicit_args,
        colors,
        speed,
        posterize,
        floyd,
        force: m.opt_present("force") && !m.opt_present("no-force"),
        skip_if_larger: m.opt_present("skip-if-larger"),
        strip: m.opt_present("strip"),
        iebug: m.opt_present("iebug"),
        last_index_transparent: m.opt_present("transbug"),
        print_help: m.opt_present("h"),
        print_version: m.opt_present("V"),
        verbose: m.opt_present("v"),

        liq: ptr::null_mut(),
        fixed_palette_image: ptr::null_mut(),
        log_callback: None,
        log_callback_user_info: ptr::null_mut(),
        fast_compression: false,
        min_quality_limit: false,
    };

    if m.opt_present("nofs") || m.opt_present("ordered") {
        options.floyd = 0.;
    }

    process::exit(unsafe {pngquant_main(&mut options)});
}


