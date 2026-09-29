import { Assert } from "std/assert"
import { Options, instantiate } from "../index"
import { ADD_MODULE, MEMORY_MODULE, NUMERIC_MODULE, START_TRAP_MODULE } from "./fixtures"

export function testCallsExportedFunction(): none {
  instance := try! instantiate(ADD_MODULE)
  values := try! instance.call("add", [20, 22])

  Assert.equal(values.length, 1)
  value := values[0] as int else { Assert.fail("add should return i32"); return }
  Assert.equal(value, 42)
  try! instance.close()
}

export function testRejectsMalformedBinary(): none {
  case instantiate([0, 97, 115, 109]) {
    _: Success -> Assert.fail("malformed binary should fail"),
    f: Failure -> {
      Assert.equal(f.error.stage, "parse")
      Assert.isTrue(f.error.message.length > 0)
    }
  }
}

export function testRejectsEmptyBinary(): none {
  case instantiate([]) {
    _: Success -> Assert.fail("empty binary should fail"),
    f: Failure -> Assert.equal(f.error.stage, "parse")
  }
}

export function testReportsMissingExport(): none {
  instance := try! instantiate(ADD_MODULE)
  case instance.call("missing") {
    _: Success -> Assert.fail("missing export should fail"),
    f: Failure -> {
      Assert.equal(f.error.stage, "lookup")
      Assert.equal(f.error.functionName, "missing")
    }
  }
}

export function testChecksArgumentTypes(): none {
  instance := try! instantiate(ADD_MODULE)
  case instance.call("add", [20L, 22L]) {
    _: Success -> Assert.fail("i64 arguments should not satisfy i32 parameters"),
    f: Failure -> Assert.equal(f.error.stage, "call")
  }
}

export function testChecksArgumentCount(): none {
  instance := try! instantiate(ADD_MODULE)
  case instance.call("add", [1]) {
    _: Success -> Assert.fail("missing argument should fail"),
    f: Failure -> Assert.stringContains(f.error.message, "expected 2 arguments")
  }
}

export function testReadsAndWritesMemory(): none {
  instance := try! instantiate(MEMORY_MODULE)
  Assert.equal(try! instance.memorySize(), 65536L)
  try! instance.writeMemory(10L, [42, 43])
  expected: readonly byte[] := [42, 43]
  Assert.arrayEqual(try! instance.readMemory(10L, 2), expected)

  values := try! instance.call("load", [10])
  value := values[0] as int else { Assert.fail("load should return i32"); return }
  Assert.equal(value, 42)
}

export function testRejectsOutOfBoundsMemoryAccess(): none {
  instance := try! instantiate(MEMORY_MODULE)
  case instance.readMemory(65536L, 1) {
    _: Success -> Assert.fail("out-of-bounds read should fail"),
    f: Failure -> Assert.equal(f.error.stage, "memory")
  }
  case instance.writeMemory(-1L, [1]) {
    _: Success -> Assert.fail("negative offset should fail"),
    f: Failure -> Assert.equal(f.error.stage, "memory")
  }
}

export function testReportsModuleWithoutMemory(): none {
  instance := try! instantiate(ADD_MODULE)
  Assert.equal(try! instance.memorySize(), 0L)
  case instance.readMemory(0L, 1) {
    _: Success -> Assert.fail("module without memory should fail"),
    f: Failure -> Assert.equal(f.error.stage, "memory")
  }
}

export function testCloseIsIdempotent(): none {
  instance := try! instantiate(ADD_MODULE)
  try! instance.close()
  try! instance.close()

  case instance.call("add", [1, 2]) {
    _: Success -> Assert.fail("closed instance should reject calls"),
    f: Failure -> Assert.equal(f.error.stage, "lifecycle")
  }
  case instance.memorySize() {
    _: Success -> Assert.fail("closed instance should reject memory access"),
    f: Failure -> Assert.equal(f.error.stage, "lifecycle")
  }
}

export function testPreservesNumericTypes(): none {
  instance := try! instantiate(NUMERIC_MODULE)

  i64Values := try! instance.call("id_i64", [9007199254740991L])
  i64 := i64Values[0] as long else { Assert.fail("id_i64 should return i64"); return }
  Assert.equal(i64, 9007199254740991L)

  f32Values := try! instance.call("id_f32", [1.5f])
  f32 := f32Values[0] as float else { Assert.fail("id_f32 should return f32"); return }
  Assert.equal(f32, 1.5f)

  f64Values := try! instance.call("id_f64", [2.25])
  f64 := f64Values[0] as double else { Assert.fail("id_f64 should return f64"); return }
  Assert.equal(f64, 2.25)
}

export function testReturnsTrapsAsErrors(): none {
  instance := try! instantiate(NUMERIC_MODULE)
  case instance.call("trap") {
    _: Success -> Assert.fail("unreachable should trap"),
    f: Failure -> {
      Assert.equal(f.error.stage, "call")
      Assert.equal(f.error.functionName, "trap")
      Assert.stringContains(f.error.message, "unreachable")
    }
  }

  // The instance remains usable after a trap.
  values := try! instance.call("id_i64", [3L])
  Assert.equal(values.length, 1)
}

export function testReportsStartFunctionTrap(): none {
  case instantiate(START_TRAP_MODULE) {
    _: Success -> Assert.fail("trapping start function should fail"),
    f: Failure -> {
      Assert.equal(f.error.stage, "start")
      Assert.stringContains(f.error.message, "unreachable")
    }
  }
}

export function testRejectsInvalidOptions(): none {
  case instantiate(ADD_MODULE, Options { stackSize: 0 }) {
    _: Success -> Assert.fail("zero stack should fail"),
    f: Failure -> Assert.equal(f.error.stage, "configure")
  }
  case instantiate(ADD_MODULE, Options { instructionLimit: 0 }) {
    _: Success -> Assert.fail("zero instruction limit should fail"),
    f: Failure -> Assert.equal(f.error.stage, "configure")
  }
}
