#![allow(non_camel_case_types)]

use std::os::raw::*;
use imagequant_sys::*;

extern "C" {
    pub fn pngquant_main(options: &mut pngquant_options) -> c_int;
}

#[repr(C)]
pub struct pngquant_options {
    pub liq: *mut liq_attr,
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
