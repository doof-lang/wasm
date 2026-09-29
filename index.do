// In-process WebAssembly loading and execution on an embedded WAMR runtime.

import { WasmError, WasmValue, configureError, decodeError, mapBytes, mapLong, mapNone, mapValue, mapValues } from "./errors"

export { WasmError, WasmValue } from "./errors"
export { ExternDescriptor, ModuleDescriptor, describe } from "./describe"

export type HostFunction = (arguments: readonly WasmValue[]): Result<readonly WasmValue[], string>

export import isolated function supportsHostImports(): bool from "./native_wasm.hpp" as doof_wasm_supports_host_imports

export class FunctionImport {
  readonly module: string
  readonly name: string
  readonly handler: HostFunction
}

export struct Options {
  stackSize: int = 65536
  memoryLimitBytes: long = 67108864L
  // Maximum Wasm instructions per call; none means unlimited.
  instructionLimit: int | none = none
  readonly imports: readonly FunctionImport[] = []
}

import class NativeWasmInstance from "./native_wasm.hpp" {
  isolated static instantiate(
    bytes: readonly byte[],
    stackSize: int,
    memoryLimitBytes: long,
    instructionLimit: int,
    imports: readonly FunctionImport[]
  ): Result<NativeWasmInstance, string>

  isolated call(functionName: string, arguments: readonly WasmValue[]): Result<WasmValue[], string>
  isolated getGlobal(name: string): Result<WasmValue, string>
  isolated setGlobal(name: string, value: WasmValue): Result<none, string>
  isolated memorySize(): Result<long, string>
  isolated readMemory(offset: long, length: int): Result<readonly byte[], string>
  isolated writeMemory(offset: long, bytes: readonly byte[]): Result<none, string>
  isolated close(): Result<none, string>
}

export class Instance {
  private native: NativeWasmInstance

  isolated call(functionName: string, arguments: readonly WasmValue[] = []): Result<WasmValue[], WasmError> {
    return mapValues(native.call(functionName, arguments))
  }

  isolated getGlobal(name: string): Result<WasmValue, WasmError> {
    return mapValue(native.getGlobal(name))
  }

  isolated setGlobal(name: string, value: WasmValue): Result<none, WasmError> {
    return mapNone(native.setGlobal(name, value))
  }

  isolated memorySize(): Result<long, WasmError> {
    return mapLong(native.memorySize())
  }

  isolated readMemory(offset: long, length: int): Result<readonly byte[], WasmError> {
    return mapBytes(native.readMemory(offset, length))
  }

  isolated writeMemory(offset: long, bytes: readonly byte[]): Result<none, WasmError> {
    return mapNone(native.writeMemory(offset, bytes))
  }

  isolated close(): Result<none, WasmError> {
    return mapNone(native.close())
  }
}

export isolated function instantiate(
  bytes: readonly byte[],
  options: Options = Options {}
): Result<Instance, WasmError> {
  if options.stackSize <= 0 {
    return Failure { error: configureError("stackSize must be greater than zero") }
  }
  if options.memoryLimitBytes <= 0L {
    return Failure { error: configureError("memoryLimitBytes must be greater than zero") }
  }
  instructionLimit := options.instructionLimit ?? -1
  if options.instructionLimit != none && instructionLimit <= 0 {
    return Failure { error: configureError("instructionLimit must be greater than zero") }
  }

  return case NativeWasmInstance.instantiate(
    bytes,
    options.stackSize,
    options.memoryLimitBytes,
    instructionLimit,
    options.imports
  ) {
    s: Success -> Success { value: Instance { native: s.value } },
    f: Failure -> Failure { error: decodeError(f.error) }
  }
}
