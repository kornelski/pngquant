use libc::{FILE, fileno, malloc};
use std::fs::File;
use std::io::prelude::*;
use std::os::unix::io::FromRawFd;
use crate::ffi::pngquant_error;

#[no_mangle]
pub extern "C" fn rwpng_read_image32_cocoa(file_handle: *mut FILE, width: &mut u32, height: &mut u32, file_size: &mut usize, out: &mut *mut cocoa_image::RGBA8) -> pngquant_error {
    let mut file = unsafe {
        File::from_raw_fd(fileno(file_handle))
    };

    let mut data = Vec::new();
    match file.read_to_end(&mut data) {
        Ok(_) => {},
        Err(_) => return pngquant_error::READ_ERROR,
    };

    let image = match cocoa_image::decode_image_as_rgba(&data) {
        Ok(img) => img,
        Err(_) => return pngquant_error::LIBPNG_FATAL_ERROR,
    };

    *file_size = data.len();
    *width = image.width() as u32;
    *height = image.height() as u32;
    unsafe {
        *out = malloc(image.buf.len() * ::std::mem::size_of::<cocoa_image::RGBA8>()) as *mut _;
        if (*out).is_null() {
            return pngquant_error::OUT_OF_MEMORY_ERROR;
        }
        ::std::slice::from_raw_parts_mut(*out, image.buf.len()).clone_from_slice(&image.buf);
    }

    pngquant_error::SUCCESS
}
