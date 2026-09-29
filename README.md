# std/wasm

Load and execute WebAssembly binaries in-process on an embedded
[WAMR](https://github.com/bytecodealliance/wasm-micro-runtime) interpreter,
including modules that spawn threads through
[wasi-threads](https://github.com/WebAssembly/wasi-threads).

Exported functions, host function imports, and globals use the four core
numeric WebAssembly types: `i32`, `i64`, `f32`, and `f64`. WASI is not
provided; `wasi.thread-spawn` is the only built-in import.

```doof
import { instantiate } from "std/wasm"

bytes: readonly byte[] := loadYourWasmBinary()
instance := try! instantiate(bytes)
results := try! instance.call("add", [20, 22])

answer := results[0] as int else { panic("add returned a non-i32 value") }
println(answer) // 42

try! instance.close()
```

## API

### Values and errors

`WasmValue` is `int | long | float | double`, corresponding to WebAssembly
`i32 | i64 | f32 | f64`. Values are never coerced: calls, host results, and
global writes require the exact declared type.

Failures are `WasmError { stage, functionName, message }`. Stages are
`configure`, `parse`, `import`, `instantiate`, `start`, `lookup`, `call`,
`host`, `memory`, `global`, and `lifecycle`.

### Instantiation

```doof
instantiate(bytes: readonly byte[], options: Options = Options {}): Result<Instance, WasmError>
```

| Option | Default | Meaning |
| --- | --- | --- |
| `stackSize` | 64 KiB | Wasm operand stack per thread |
| `memoryLimitBytes` | 64 MiB | Cap on linear memory, including growth |
| `instructionLimit` | `none` | Maximum instructions per `call`; exceeding it fails the call |
| `imports` | `[]` | Host function imports |

Instantiation parses the binary, links imports, creates memory, and runs the
start function. A module whose initial memory exceeds `memoryLimitBytes` is
rejected; `memory.grow` beyond it returns -1. The binary is copied, so the
input bytes need not outlive the call.

### Host function imports

`Options.imports` binds imported functions by their exact module and field
names:

```doof
import { FunctionImport, Options, WasmValue, instantiate } from "std/wasm"

isolated function multiply(arguments: readonly WasmValue[]): Result<readonly WasmValue[], string> {
  left := arguments[0] as int else error { return Failure { error } }
  right := arguments[1] as int else error { return Failure { error } }
  return Success { value: [left * right] }
}

instance := try! instantiate(bytes, Options {
  imports: [FunctionImport { module: "env", name: "multiply", handler: multiply }]
})
```

- Signatures come from the binary. A handler must return exactly the declared
  results; failures, panics, and wrong results become `host` errors naming the
  import.
- Host imports may return at most one value. WAMR's interpreter cannot pass
  more back from the host, so such imports are rejected at instantiation.
- An unconfigured function import fails with a `call` error when execution
  first reaches it. Configuring an import the module does not declare is an
  `import` error. Global and table imports are not supported.
- Handlers run synchronously on the thread that called `instance.call`, so
  they may read and write the instance's memory. Calling back into the same
  instance, or closing it, from a handler is rejected.
- Handlers cannot run on threads the module spawns; see below.

### Threads

Modules built for wasi-threads (for example the `wasm32-wasip1-threads`
target) run as-is. They import a shared memory (`env.memory`), import
`wasi.thread-spawn`, and export `wasi_thread_start(tid, arg)`. The imported
shared memory is created from its declared limits, capped by
`memoryLimitBytes`. Atomics and `memory.atomic.wait`/`notify` are supported.

- `call` returns when the exported function returns. Spawned threads keep
  running afterwards and can be joined in a later call.
- A trap in any thread terminates all of the instance's threads, and the
  running call fails with that trap.
- At most 64 threads, including the calling thread, can run per instance;
  `thread-spawn` returns a negative value beyond that.
- `close()` terminates every spawned thread and waits for them to exit.
- A host import called from a spawned thread fails with a `host` error. Doof
  function values are bound to the actor domain that created them, and Doof
  has no function type yet that promises a callback is safe to run on another
  thread.

### Calling exports

```doof
instance.call(name: string, arguments: readonly WasmValue[] = []): Result<WasmValue[], WasmError>
```

Multiple return values are preserved in declaration order. Traps are `call`
errors, and the instance remains usable after one.

### Globals

```doof
instance.getGlobal(name: string): Result<WasmValue, WasmError>
instance.setGlobal(name: string, value: WasmValue): Result<none, WasmError>
```

These access exported globals. Writing an immutable global fails.

### Linear memory

```doof
instance.memorySize(): Result<long, WasmError>
instance.readMemory(offset: long, length: int): Result<readonly byte[], WasmError>
instance.writeMemory(offset: long, bytes: readonly byte[]): Result<none, WasmError>
```

Memory access uses memory index 0, exported or not, and is bounds-checked.
`memorySize` is 0 for a module without memory, and reads and writes fail.

### Describing a module

```doof
describe(bytes: readonly byte[]): Result<ModuleDescriptor, WasmError>
```

This returns the imports and exports of a binary without instantiating it.
Each `ExternDescriptor` has `module` (empty for exports), `name`, `kind`
(`function`, `memory`, `global`, or `table`), function `params` and
`results` (or a global's value type in `results`), and `mutable` for globals.

### Lifecycle

`instance.close()` is idempotent. Other operations return a `lifecycle` error
after close. Native resources are also released when the last reference is
dropped, but explicit close is recommended for deterministic cleanup.

## Runtime

WAMR's fast interpreter is embedded at a pinned revision; see
`THIRD_PARTY.md`. Operations on one `Instance` are serialized, so it can be
shared with actor-dispatched code. Different instances run in parallel.

Supported hosts are macOS, iOS, and Linux; Windows is configured but
untested. `std/wasm` cannot itself be compiled to WebAssembly.
`supportsHostImports()` is kept for compatibility and is always true.

## Development

Tests use hand-written modules in `tests/wat/`. After editing one, regenerate
`tests/fixtures.do` with `tests/wat/generate.sh`, which needs
[wasm-tools](https://github.com/bytecodealliance/wasm-tools).
