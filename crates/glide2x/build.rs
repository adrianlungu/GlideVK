// Glide 2.x games import stdcall-decorated names (`_grXxx@N`). Rust exports the
// plain symbols; this feeds the linker a .def that adds the decorated names as
// aliases. Only relevant for the Windows/Wine PE build.
fn main() {
    println!("cargo:rerun-if-changed=glide2x.def");
    let target = std::env::var("TARGET").unwrap_or_default();
    if target.contains("windows") {
        let def = format!("{}/glide2x.def", env!("CARGO_MANIFEST_DIR"));
        println!("cargo:rustc-cdylib-link-arg={def}");
    }
}
