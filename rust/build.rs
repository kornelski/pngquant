extern crate gcc;
extern crate pkg_config;

#[cfg(feature = "lcms2")]
extern crate lcms2_sys;

use std::env;

fn probe(lib: &str, ver: &str) -> Option<pkg_config::Library> {
    let statik = cfg!(feature = "static");
    let mut pkg = pkg_config::Config::new();
    pkg.atleast_version(ver);
    pkg.statik(statik);
    match pkg.probe(lib) {
        Ok(lib) => Some(lib),
        Err(pkg_config::Error::Failure{output,..}) => {
            println!("cargo:warning={}", String::from_utf8_lossy(&output.stderr).trim_right().replace("\n", "\ncargo:warning="));
            None
        },
        Err(err) => {
            println!("cargo:warning=Can't find {} v{}: {:?}", lib, ver, err);
            None
        }
    }
}

fn main() {
    let libpng = probe("libpng", "1.4").unwrap();
    let mut cc = gcc::Config::new();

    // Muahahaha
    cc.define("main", Some("pngquant_main"));

    if cfg!(feature = "cocoa") {
        if cfg!(feature = "lcms2") {
            println!("cargo:warning=Don't use both lcms2 and cocoa features at the same time, see --no-default-features");
        }
        println!("cargo:rustc-link-lib=framework=Cocoa");

        cc.define("USE_COCOA", Some("1"));
        cc.file("rwpng_cocoa.m");
    }
    else if cfg!(feature = "lcms2") {
        if let Ok(path) = env::var("DEP_LCMS2_INCLUDE") {
            cc.include(path);
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

    for p in libpng.include_paths {
        cc.include(p);
    }

    cc.compile("libpngquant.a");
}
