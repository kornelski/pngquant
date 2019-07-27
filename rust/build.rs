use std::env;
use std::path::Path;

fn main() {
    let target_arch = env::var("CARGO_CFG_TARGET_ARCH").expect("CARGO_CFG_TARGET_ARCH not set");
    let mut cc = cc::Build::new();
    cc.warnings(false);

    cc.define("PNGQUANT_NO_MAIN", Some("1"));

    if cfg!(feature = "openmp") {
        cc.flag(&env::var("DEP_OPENMP_FLAG").unwrap());
    }

    if cfg!(feature = "cocoa") {
        if cfg!(feature = "lcms2") {
            println!("cargo:warning=Don't use both lcms2 and cocoa features at the same time, see --no-default-features");
        }
        cc.define("USE_COCOA", Some("1"));
    } else if cfg!(feature = "lcms2") {
        if let Ok(p) = env::var("DEP_LCMS2_INCLUDE") {
            cc.include(dunce::simplified(Path::new(&p)));
        }
        cc.define("USE_LCMS", Some("1"));
    }

    if env::var("PROFILE").map(|p| p != "debug").unwrap_or(true) {
        cc.define("NDEBUG", Some("1"));
    } else {
        cc.define("DEBUG", Some("1"));
    }

    if target_arch == "x86_64" ||
       (target_arch == "x86" && cfg!(feature = "sse")) {
        cc.define("USE_SSE", Some("1"));
    }

    cc.file("rwpng.c");
    cc.file("pngquant.c");

    if let Ok(p) = env::var("DEP_IMAGEQUANT_INCLUDE") {
        cc.include(dunce::simplified(Path::new(&p)));
    } else {
        cc.include("lib");
    }

    if let Ok(p) = env::var("DEP_PNG_INCLUDE") {
        for p in env::split_paths(&p) {
            cc.include(dunce::simplified(&p));
        }
    }

    cc.compile("libpngquant.a");
}
