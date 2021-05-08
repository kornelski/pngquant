#![allow(non_camel_case_types)]

use libc::FILE;
use std::os::raw::*;
use imagequant_sys::*;

extern "C" {
    pub static PNGQUANT_VERSION: *const c_char;
    pub static PNGQUANT_USAGE: *const c_char;
    pub fn pngquant_internal_print_config(fd: *mut libc::FILE);

    pub fn pngquant_main_internal(options: &mut pngquant_options, liq: *mut liq_attr) -> pngquant_error;
    pub fn pngquant_c_stderr() -> *mut FILE;
    pub fn pngquant_c_stdout() -> *mut FILE;
}

#[repr(C)]
#[derive(Debug, Copy, Clone)]
#[allow(dead_code)]
#[allow(non_camel_case_types)]
pub enum pngquant_error {
    SUCCESS = 0,
    MISSING_ARGUMENT = 1,
    READ_ERROR = 2,
    INVALID_ARGUMENT = 4,
    NOT_OVERWRITING_ERROR = 15,
    CANT_WRITE_ERROR = 16,
    OUT_OF_MEMORY_ERROR = 17,
    WRONG_ARCHITECTURE = 18, // Missing SSE
    PNG_OUT_OF_MEMORY_ERROR = 24,
    LIBPNG_FATAL_ERROR = 25,
    WRONG_INPUT_COLOR_TYPE = 26,
    LIBPNG_INIT_ERROR = 35,
    TOO_LARGE_FILE = 98,
    TOO_LOW_QUALITY = 99,
}

#[repr(C)]
pub struct pngquant_options {
    pub fixed_palette_image: *mut liq_image,
    pub log_callback: liq_log_callback_function,
    pub log_callback_user_info: *mut c_void,
    pub quality: *const c_char,
    pub extension: *const c_char,
    pub output_file_path: *const c_char,
    pub map_file: *const c_char,
    pub files: *const *const c_char,
    pub num_files: c_uint,
    pub colors: c_uint,
    pub speed: c_uint,
    pub posterize: c_uint,
    pub floyd: f32,
    pub using_stdin: bool,
    pub using_stdout: bool,
    pub force: bool,
    pub fast_compression: bool,
    pub min_quality_limit: bool,
    pub skip_if_larger: bool,
    pub strip: bool,
    pub iebug: bool,
    pub last_index_transparent: bool,
    pub print_help: bool,
    pub print_version: bool,
    pub missing_arguments: bool,
    pub verbose: bool,
}
