# The anonymous GN source package

`gn-src.tar.gz` is the package to upload. `scripts/` holds the scripts that made it from the Camel tree. They name what they replace, so they must not be uploaded.

## What it is

- **Source:** commit `ae874ce04` of the Camel tree, the build the study used, exported without git history.
- **Removed parts:** the GPU tiles, the C++ translator, the Python embedding and binding, the `sim` and `ckpt` modules, the formatter, the editor tools, and the repository's maintenance scripts and bookkeeping tests.
- **Kept:** the parts the study used:
  - the front end;
  - the six pipelines (`std::macro`, `std::opt`, `std::nvm`, `std::fvm`, `std::jit`);
  - the parallel scheduler and the ONNX export;
  - `autodiff`, `transforms`, `tensor`, `nn` and `random`;
  - the rest of the standard library.
- **Renamed:** Camel/OpenCML became GN, the binary `camel` became `gn`, `.cml` became `.gn`, and the copyright line became "the GN authors".
- **Scrubbed:** author names, the repository owner and address, branch names, host paths and archive paths.
- **Docs:** the language reference and sections 1–12 of the design principles, with dated rulings and links to internal documents removed.
- **Version string:** the binary reports `GN v0.2.43` and a build date. It names no commit.

## Size of the package

- **C++:** about 318,000 lines:
  - `src` 198,478;
  - `include` 48,228;
  - `modules` 67,663;
  - `tools` 3,697.
- **Standard library in GN:** 44 files, 10,733 lines.
- **Tests:** 3,371 test programs in 318 plans.

The full implementation is about 360,000 lines of C++.

## Same semantics as the study's build

- **Pipeline comparison:** 400 test programs were run on `ae874ce04` and on the package under three pipelines, 1,200 runs in all. No run differed in meaning. The differences were:
  - the list of module search paths in "module not found" messages;
  - programs importing a removed module;
  - one diagnostic column, shifted because a variable named `camel_loss` became `gn_loss`.
- **Build and test from the tarball:** the tarball was extracted into an empty directory and built with `node scripts/build.js`. The fast tier then ran 7,763 tests:
  - 7,711 pass, 30 are known failures that their plans record, and 15 are skipped;
  - 7 fail, all of them timing-margin checks that failed under load.

  `TEST-STATUS.md` inside the package lists them.
