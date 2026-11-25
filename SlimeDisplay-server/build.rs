//! Platform-specific linking for the SlimeDisplay native library, plus optional
//! bindgen generation of the FFI bindings.
//!
//! Environment variables:
//! - `SLIMEDISPLAY_LIB_DIR`: directory containing `SlimeDisplay.lib`/`.dll` (Windows)
//!   or `SlimeDisplay.framework` (macOS).
//! - `SLIMEDISPLAY_INCLUDE_DIR`: directory containing `slimedisplay.h` (bindgen only).

use std::env;

fn main() {
    println!("cargo:rerun-if-changed=build.rs");
    println!("cargo:rerun-if-env-changed=SLIMEDISPLAY_LIB_DIR");
    println!("cargo:rerun-if-env-changed=SLIMEDISPLAY_INCLUDE_DIR");

    if env::var_os("CARGO_FEATURE_NATIVE").is_none() {
        return;
    }

    link_native();

    #[cfg(feature = "bindgen")]
    generate_bindings();
}

fn link_native() {
    // build.rs runs on the host, so read the *target* OS from Cargo instead of #[cfg].
    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    let lib_dir = env::var("SLIMEDISPLAY_LIB_DIR").ok();

    match target_os.as_str() {
        "windows" => {
            if let Some(dir) = &lib_dir {
                println!("cargo:rustc-link-search=native={dir}");
            }
            // Links the SlimeDisplay.lib import library; SlimeDisplay.dll must be
            // discoverable at runtime (next to the executable or on PATH).
            println!("cargo:rustc-link-lib=dylib=SlimeDisplay");
        }
        "macos" => {
            if let Some(dir) = &lib_dir {
                println!("cargo:rustc-link-search=framework={dir}");
                println!("cargo:rustc-link-arg=-Wl,-rpath,{dir}");
            }
            println!("cargo:rustc-link-lib=framework=SlimeDisplay");
            for framework in [
                "Foundation",
                "CoreGraphics",
                "CoreMedia",
                "CoreVideo",
                "VideoToolbox",
                "ScreenCaptureKit",
            ] {
                println!("cargo:rustc-link-lib=framework={framework}");
            }
        }
        other => panic!("SlimeDisplay native library is not available for target OS `{other}`"),
    }
}

#[cfg(feature = "bindgen")]
fn generate_bindings() {
    use std::path::PathBuf;

    let include_dir = env::var("SLIMEDISPLAY_INCLUDE_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|_| PathBuf::from("../SlimeDisplay-mac-core/SlimeDisplay-mac-core/Public"));
    let header = include_dir.join("slimedisplay.h");
    println!("cargo:rerun-if-changed={}", header.display());
    println!(
        "cargo:rerun-if-changed={}",
        include_dir.join("slimedisplay_types.h").display()
    );

    // Constified enums with unprefixed names, so the output matches the
    // hand-maintained bindings (`pub const SD_SUCCESS: SD_Result = 0;`).
    let bindings = bindgen::Builder::default()
        .header(header.to_string_lossy())
        .clang_arg(format!("-I{}", include_dir.display()))
        .allowlist_function("sd_.*")
        .allowlist_type("SD_.*")
        .allowlist_var("SD_.*")
        .prepend_enum_name(false)
        .generate()
        .expect("failed to generate SlimeDisplay bindings");

    let out = PathBuf::from(env::var("OUT_DIR").unwrap()).join("slimedisplay_bindings.rs");
    bindings
        .write_to_file(out)
        .expect("failed to write SlimeDisplay bindings");
}
