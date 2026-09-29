// Import and export descriptions of a WebAssembly binary.

import { WasmError, mapStrings } from "./errors"

import isolated function nativeDescribe(bytes: readonly byte[]): Result<readonly string[], string> from "./native_wasm_engine.hpp" as doof_wasm_describe

// Value types are "i32", "i64", "f32", "f64", "v128", "externref", or "funcref".
export class ExternDescriptor {
  // Import namespace; empty for exports.
  readonly module: string
  readonly name: string
  // "function", "memory", "global", or "table".
  readonly kind: string
  // Function parameter types; empty for other kinds.
  readonly params: readonly string[]
  // Function result types, or the value type of a global.
  readonly results: readonly string[]
  // Whether a global is mutable; false for other kinds.
  readonly mutable: bool
}

export class ModuleDescriptor {
  imports: ExternDescriptor[]
  exports: ExternDescriptor[]
}

// Fields per descriptor in the native encoding:
// direction, module, name, kind, params, results, mutable.
readonly FIELD_COUNT = 7

// Parses a binary and describes its imports and exports without instantiating it.
export isolated function describe(bytes: readonly byte[]): Result<ModuleDescriptor, WasmError> {
  try fields := mapStrings(nativeDescribe(bytes))

  let imports: ExternDescriptor[] = []
  let exports: ExternDescriptor[] = []
  let index = 0
  while index + FIELD_COUNT <= fields.length {
    descriptor := ExternDescriptor {
      module: fields[index + 1],
      name: fields[index + 2],
      kind: fields[index + 3],
      params: splitTypes(fields[index + 4]),
      results: splitTypes(fields[index + 5]),
      mutable: fields[index + 6] == "mutable",
    }
    if fields[index] == "import" {
      imports.push(descriptor)
    } else {
      exports.push(descriptor)
    }
    index += FIELD_COUNT
  }
  return Success { value: ModuleDescriptor { imports, exports } }
}

isolated function splitTypes(joined: string): readonly string[] {
  if joined.length == 0 {
    return []
  }
  return joined.split(",")
}
