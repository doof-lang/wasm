import { Assert } from "std/assert"
import { FunctionImport, Options, WasmValue, instantiate, supportsHostImports } from "../index"
import { IMPORTS_MODULE, MULTI_RESULT_IMPORT_MODULE, TYPED_IMPORTS_MODULE } from "./fixtures"

function multiplyHost(arguments: readonly WasmValue[]): Result<readonly WasmValue[], string> {
  left := arguments[0] as int else error { return Failure { error } }
  right := arguments[1] as int else error { return Failure { error } }
  return Success { value: [left * right] }
}

function failingHost(arguments: readonly WasmValue[]): Result<readonly WasmValue[], string> {
  return Failure { error: "host refused the call" }
}

function wrongTypeHost(arguments: readonly WasmValue[]): Result<readonly WasmValue[], string> {
  return Success { value: [42L] }
}

function wrongCountHost(arguments: readonly WasmValue[]): Result<readonly WasmValue[], string> {
  return Success { value: [] }
}

function panickingHost(arguments: readonly WasmValue[]): Result<readonly WasmValue[], string> {
  panic("host callback panic")
}

function echoHost(arguments: readonly WasmValue[]): Result<readonly WasmValue[], string> {
  return Success { value: arguments }
}

function multiplyOptions(handler: (arguments: readonly WasmValue[]): Result<readonly WasmValue[], string>): Options {
  return Options {
    imports: [FunctionImport { module: "env", name: "multiply", handler }]
  }
}

function expectHostFailure(handler: (arguments: readonly WasmValue[]): Result<readonly WasmValue[], string>, expected: string): none {
  instance := try! instantiate(IMPORTS_MODULE, multiplyOptions(handler))
  case instance.call("run", [21]) {
    _: Success -> Assert.fail("host failure should fail the Wasm call"),
    f: Failure -> {
      Assert.equal(f.error.stage, "host")
      Assert.equal(f.error.functionName, "env.multiply")
      Assert.stringContains(f.error.message, expected)
    }
  }
}

export function testSupportsHostImports(): none {
  Assert.isTrue(supportsHostImports())
}

export function testCallsHostFunctionImport(): none {
  instance := try! instantiate(IMPORTS_MODULE, multiplyOptions(multiplyHost))
  values := try! instance.call("run", [21])
  value := values[0] as int else { Assert.fail("run should return i32"); return }
  Assert.equal(value, 42)
}

export function testReportsMissingHostFunctionWhenCalled(): none {
  instance := try! instantiate(IMPORTS_MODULE)
  case instance.call("run", [21]) {
    _: Success -> Assert.fail("unresolved import should fail when called"),
    f: Failure -> {
      Assert.equal(f.error.stage, "call")
      Assert.stringContains(f.error.message, "missing imported function env.multiply")
    }
  }
}

export function testReturnsHostFailureAsStructuredError(): none {
  expectHostFailure(failingHost, "host refused the call")
}

export function testRejectsWrongHostResultType(): none {
  expectHostFailure(wrongTypeHost, "wrong type")
}

export function testRejectsWrongHostResultCount(): none {
  expectHostFailure(wrongCountHost, "expected 1 results, received 0")
}

export function testReturnsHostPanicAsStructuredError(): none {
  expectHostFailure(panickingHost, "host callback panic")
}

export function testRejectsUnknownConfiguredImport(): none {
  options := Options {
    imports: [FunctionImport { module: "env", name: "missing", handler: multiplyHost }]
  }
  case instantiate(IMPORTS_MODULE, options) {
    _: Success -> Assert.fail("unknown configured import should fail"),
    f: Failure -> {
      Assert.equal(f.error.stage, "import")
      Assert.stringContains(f.error.message, "env.missing")
    }
  }
}

export function testRejectsDuplicateConfiguredImport(): none {
  options := Options {
    imports: [
      FunctionImport { module: "env", name: "multiply", handler: multiplyHost },
      FunctionImport { module: "env", name: "multiply", handler: echoHost },
    ]
  }
  case instantiate(IMPORTS_MODULE, options) {
    _: Success -> Assert.fail("duplicate import should fail"),
    f: Failure -> Assert.equal(f.error.stage, "import")
  }
}

export function testRejectsMultiResultHostImport(): none {
  options := Options {
    imports: [FunctionImport { module: "env", name: "pair", handler: echoHost }]
  }
  case instantiate(MULTI_RESULT_IMPORT_MODULE, options) {
    _: Success -> Assert.fail("multi-result host import should fail"),
    f: Failure -> {
      Assert.equal(f.error.stage, "import")
      Assert.stringContains(f.error.message, "at most one")
    }
  }
}

export function testHostImportsPreserveAllNumericTypes(): none {
  options := Options {
    imports: [
      FunctionImport { module: "env", name: "i32", handler: echoHost },
      FunctionImport { module: "env", name: "i64", handler: echoHost },
      FunctionImport { module: "env", name: "f32", handler: echoHost },
      FunctionImport { module: "env", name: "f64", handler: echoHost },
    ]
  }
  instance := try! instantiate(TYPED_IMPORTS_MODULE, options)
  values := try! instance.call("run", [7, 8L, 1.5f, 2.25])

  Assert.equal(values.length, 4)
  i32 := values[0] as int else { Assert.fail("result 0 should be i32"); return }
  i64 := values[1] as long else { Assert.fail("result 1 should be i64"); return }
  f32 := values[2] as float else { Assert.fail("result 2 should be f32"); return }
  f64 := values[3] as double else { Assert.fail("result 3 should be f64"); return }
  Assert.equal(i32, 7)
  Assert.equal(i64, 8L)
  Assert.equal(f32, 1.5f)
  Assert.equal(f64, 2.25)
}
