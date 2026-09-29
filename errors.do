// Structured errors for std/wasm and decoding of native error strings.

export type WasmValue = int | long | float | double

export class WasmError {
  stage: string
  functionName: string | none
  message: string
}

// Native errors arrive as "stage|function|message".
export function decodeError(raw: string): WasmError {
  first := raw.indexOf("|")
  if first < 0 {
    return WasmError { stage: "runtime", functionName: none, message: raw }
  }

  stage := raw.substring(0, first)
  remainder := raw.slice(first + 1)
  second := remainder.indexOf("|")
  if second < 0 {
    return WasmError { stage, functionName: none, message: remainder }
  }

  functionText := remainder.substring(0, second)
  return WasmError {
    stage,
    functionName: if functionText.length > 0 then functionText else none,
    message: remainder.slice(second + 1),
  }
}

export function configureError(message: string): WasmError {
  return WasmError { stage: "configure", functionName: none, message }
}

export function mapValues(result: Result<WasmValue[], string>): Result<WasmValue[], WasmError> {
  return case result {
    s: Success -> Success { value: s.value },
    f: Failure -> Failure { error: decodeError(f.error) }
  }
}

export function mapValue(result: Result<WasmValue, string>): Result<WasmValue, WasmError> {
  return case result {
    s: Success -> Success { value: s.value },
    f: Failure -> Failure { error: decodeError(f.error) }
  }
}

export function mapLong(result: Result<long, string>): Result<long, WasmError> {
  return case result {
    s: Success -> Success { value: s.value },
    f: Failure -> Failure { error: decodeError(f.error) }
  }
}

export function mapBytes(result: Result<readonly byte[], string>): Result<readonly byte[], WasmError> {
  return case result {
    s: Success -> Success { value: s.value },
    f: Failure -> Failure { error: decodeError(f.error) }
  }
}

export function mapStrings(result: Result<readonly string[], string>): Result<readonly string[], WasmError> {
  return case result {
    s: Success -> Success { value: s.value },
    f: Failure -> Failure { error: decodeError(f.error) }
  }
}

export function mapNone(result: Result<none, string>): Result<none, WasmError> {
  return case result {
    _: Success -> Success(),
    f: Failure -> Failure { error: decodeError(f.error) }
  }
}
