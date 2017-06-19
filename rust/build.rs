extern crate gcc;

use std::env;

fn fudge_windows_unc_path(path: &str) -> &str {
    if path.starts_with("\\\\?\\") {
        &path[4..]
    } else {
        path
    }
}

fn main() {
    let mut cc = gcc::Config::new();

    cc.define("PNGQUANT_NO_MAIN", Some("1"));

    if cfg!(feature = "cocoa") {
        if cfg!(feature = "lcms2") {
            println!("cargo:warning=Don't use both lcms2 and cocoa features at the same time, see --no-default-features");
        }
        println!("cargo:rustc-link-lib=framework=Cocoa");

        cc.define("USE_COCOA", Some("1"));
        cc.file("rwpng_cocoa.m");
    }
    else if cfg!(feature = "lcms2") {
        if let Ok(p) = env::var("DEP_LCMS2_INCLUDE") {
            cc.include(fudge_windows_unc_path(&p));
        }
        cc.define("USE_LCMS", Some("1"));
    }

    if env::var("PROFILE").map(|p|p != "debug").unwrap_or(true) {
        cc.define("NDEBUG", Some("1"));
    }

    if cfg!(target_arch="x86_64") ||
       (cfg!(target_arch="x86") && cfg!(feature = "sse")) {
        cc.define("USE_SSE", Some("1"));
    }

    cc.file("rwpng.c");
    cc.file("pngquant.c");

    if let Ok(p) = env::var("DEP_IMAGEQUANT_INCLUDE") {
        cc.include(fudge_windows_unc_path(&p));
    } else {
        cc.include("lib");
    }

    if let Ok(p) = env::var("DEP_LIBPNG_INCLUDE") {
        cc.include(fudge_windows_unc_path(&p));
    }

    cc.compile("libpngquant.a");
}
