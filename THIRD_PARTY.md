# Third-party software

`vendor/wamr` contains the WebAssembly Micro Runtime (WAMR), licensed under
the Apache License 2.0 with LLVM exception. `vendor/wamr/ATTRIBUTIONS.md` lists
the third-party components WAMR itself includes.

- Upstream: https://github.com/bytecodealliance/wasm-micro-runtime
- Release: `WAMR-2.4.5`
- Revision: `25bd7eb63e828e4bd242cc9b38d260b4b31c6605`
- Imported: 2026-09-29
- Included paths: listed under `keep` in `doof-vendor.json`

Only the fast interpreter, the wasm-c-api, the thread manager, wasi-threads,
and the Darwin, Linux, and Windows platform layers are vendored. AOT, JIT,
WASI (libc-wasi), and the assembly `invokeNative` trampolines are not built.

`native_wasm_wamr.c` reads WAMR's internal C-API structures, so after
refreshing the vendored revision, check that file against
`core/iwasm/common/wasm_c_api_internal.h` and
`core/iwasm/libraries/lib-wasi-threads/lib_wasi_threads_wrapper.c`, then run
`DOOF_STDLIB_ROOT=$PWD doof test wasm` from the parent standard-library
workspace.
