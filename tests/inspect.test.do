import { Assert } from "std/assert"
import { intValue, longValue } from "./values"
import { ExternDescriptor, Options, describe, instantiate } from "../index"
import { ADD_MODULE, GLOBALS_MODULE, LIMITS_MODULE, THREADS_MODULE } from "./fixtures"

function find(descriptors: ExternDescriptor[], name: string): ExternDescriptor | none {
  for descriptor of descriptors {
    if descriptor.name == name {
      return descriptor
    }
  }
  return none
}

export function testDescribesExports(): none {
  module := try! describe(ADD_MODULE)
  Assert.equal(module.imports.length, 0)
  Assert.equal(module.exports.length, 1)

  add := module.exports[0]
  Assert.equal(add.name, "add")
  Assert.equal(add.kind, "function")
  expectedParams: readonly string[] := ["i32", "i32"]
  expectedResults: readonly string[] := ["i32"]
  Assert.arrayEqual(add.params, expectedParams)
  Assert.arrayEqual(add.results, expectedResults)
}

export function testDescribesImportsOfThreadedModule(): none {
  module := try! describe(THREADS_MODULE)
  Assert.equal(module.imports.length, 3)

  memory := find(module.imports, "memory") else { Assert.fail("memory import missing"); return }
  Assert.equal(memory.module, "env")
  Assert.equal(memory.kind, "memory")

  spawn := find(module.imports, "thread-spawn") else { Assert.fail("thread-spawn import missing"); return }
  Assert.equal(spawn.module, "wasi")
  Assert.equal(spawn.kind, "function")
}

export function testDescribesGlobals(): none {
  module := try! describe(GLOBALS_MODULE)
  counter := find(module.exports, "counter") else { Assert.fail("counter export missing"); return }
  Assert.equal(counter.kind, "global")
  Assert.isTrue(counter.mutable)
  expected: readonly string[] := ["i32"]
  Assert.arrayEqual(counter.results, expected)

  limit := find(module.exports, "limit") else { Assert.fail("limit export missing"); return }
  Assert.isFalse(limit.mutable)
}

export function testDescribeRejectsMalformedBinary(): none {
  case describe([1, 2, 3]) {
    _: Success -> Assert.fail("malformed binary should fail"),
    f: Failure -> Assert.equal(f.error.stage, "parse")
  }
}

export function testReadsAndWritesGlobals(): none {
  instance := try! instantiate(GLOBALS_MODULE)
  counter := try! instance.getGlobal("counter")
  Assert.equal(intValue(counter), 5)

  try! instance.setGlobal("counter", 11)
  values := try! instance.call("counter_value")
  Assert.equal(intValue(values[0]), 11)

  limit := try! instance.getGlobal("limit")
  Assert.equal(longValue(limit), 9L)
}

export function testRejectsInvalidGlobalWrites(): none {
  instance := try! instantiate(GLOBALS_MODULE)
  case instance.setGlobal("limit", 1L) {
    _: Success -> Assert.fail("immutable global should reject writes"),
    f: Failure -> Assert.equal(f.error.stage, "global")
  }
  case instance.setGlobal("counter", 1L) {
    _: Success -> Assert.fail("i64 should not satisfy an i32 global"),
    f: Failure -> Assert.equal(f.error.stage, "global")
  }
  case instance.getGlobal("missing") {
    _: Success -> Assert.fail("missing global should fail"),
    f: Failure -> Assert.equal(f.error.stage, "lookup")
  }
}

export function testInstructionLimitStopsRunawayCalls(): none {
  instance := try! instantiate(LIMITS_MODULE, Options { instructionLimit: 100000 })
  case instance.call("spin") {
    _: Success -> Assert.fail("infinite loop should hit the instruction limit"),
    f: Failure -> {
      Assert.equal(f.error.stage, "call")
      Assert.stringContains(f.error.message, "instruction limit")
    }
  }

  // Each call has its own budget.
  values := try! instance.call("count", [10])
  Assert.equal(intValue(values[0]), 10)
}

export function testMemoryLimitCapsGrowth(): none {
  instance := try! instantiate(LIMITS_MODULE, Options { memoryLimitBytes: 131072L })
  first := try! instance.call("grow", [1])
  Assert.equal(intValue(first[0]), 1)
  second := try! instance.call("grow", [1])
  Assert.equal(intValue(second[0]), -1)
  Assert.equal(try! instance.memorySize(), 131072L)
}

export function testRejectsInitialMemoryAboveLimit(): none {
  case instantiate(LIMITS_MODULE, Options { memoryLimitBytes: 1024L }) {
    _: Success -> Assert.fail("initial memory above the limit should fail"),
    f: Failure -> Assert.equal(f.error.stage, "instantiate")
  }
}
