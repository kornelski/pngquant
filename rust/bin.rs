extern crate imagequant_sys;
extern crate libpng_sys;

#[cfg(feature = "lcms2")]
extern crate lcms2_sys;

use std::os::raw::c_int;
use std::env;
use std::ffi::CString;

extern "C" {
    fn pngquant_main(argc: c_int, argv: *mut *mut u8) -> c_int;
}

fn main() {
    let mut args: Vec<_> = env::args().map(|c| CString::new(c.as_bytes()).unwrap().into_bytes_with_nul()).collect();
    let mut argv: Vec<_> = args.iter_mut().map(|arg| arg.as_mut_ptr()).collect();
    unsafe {
        pngquant_main(argv.len() as c_int, argv.as_mut_ptr());
    }
}
