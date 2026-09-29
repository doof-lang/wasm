import { Assert } from "std/assert"
import { intValue } from "./values"
import { FunctionImport, Options, WasmValue, instantiate } from "../index"
import { THREADS_MODULE } from "./fixtures"

// Worker modes understood by tests/wat/threads.wat.
readonly ADD = 0
readonly CALL_HOST = 1
readonly SPIN = 2
readonly WAIT = 3

isolated function noteHost(arguments: readonly WasmValue[]): Result<readonly WasmValue[], string> {
  return Success { value: [] }
}

function threadedOptions(): Options {
  return Options {
    imports: [FunctionImport { module: "env", name: "note", handler: noteHost }]
  }
}

function sumTo(count: int): int {
  return count * (count + 1) \ 2
}

export function testRunsWasiThreads(): none {
  instance := try! instantiate(THREADS_MODULE, threadedOptions())
  values := try! instance.call("run", [4, ADD])
  Assert.equal(intValue(values[0]), sumTo(4))
  try! instance.close()
}

export function testReusesInstanceAcrossThreadedCalls(): none {
  instance := try! instantiate(THREADS_MODULE, threadedOptions())
  for round of 1..5 {
    values := try! instance.call("run", [8, ADD])
    Assert.equal(intValue(values[0]), sumTo(8))
  }
}

export function testSharedMemoryIsVisibleToHost(): none {
  instance := try! instantiate(THREADS_MODULE, threadedOptions())
  try! instance.call("run", [3, ADD])
  bytes := try! instance.readMemory(0L, 8)
  // Little-endian sum (6) followed by the finished-worker count (3).
  expected: readonly byte[] := [6, 0, 0, 0, 3, 0, 0, 0]
  Assert.arrayEqual(bytes, expected)
}

export function testHostImportsRunOnCallingThread(): none {
  instance := try! instantiate(THREADS_MODULE, threadedOptions())
  values := try! instance.call("note", [5])
  Assert.equal(values.length, 0)
}

export function testRejectsHostImportsFromSpawnedThreads(): none {
  instance := try! instantiate(THREADS_MODULE, threadedOptions())
  case instance.call("run", [2, CALL_HOST]) {
    _: Success -> Assert.fail("host import from a spawned thread should fail"),
    f: Failure -> {
      Assert.equal(f.error.stage, "host")
      Assert.equal(f.error.functionName, "env.note")
      Assert.stringContains(f.error.message, "threads spawned by the module")
    }
  }
}

export function testReportsSpawnFailureBeyondThreadLimit(): none {
  instance := try! instantiate(THREADS_MODULE, threadedOptions())
  values := try! instance.call("detach", [100, WAIT])
  Assert.equal(intValue(values[0]), -1)
  try! instance.close()
}

export function testCloseTerminatesSpinningThreads(): none {
  instance := try! instantiate(THREADS_MODULE, threadedOptions())
  values := try! instance.call("detach", [2, SPIN])
  Assert.equal(intValue(values[0]), 0)
  try! instance.close()
}

export function testCloseTerminatesWaitingThreads(): none {
  instance := try! instantiate(THREADS_MODULE, threadedOptions())
  values := try! instance.call("detach", [2, WAIT])
  Assert.equal(intValue(values[0]), 0)
  try! instance.close()
}

export function testRejectsSharedMemoryAboveLimit(): none {
  case instantiate(THREADS_MODULE, Options { memoryLimitBytes: 1024L }) {
    _: Success -> Assert.fail("shared memory above the limit should fail"),
    f: Failure -> Assert.equal(f.error.stage, "instantiate")
  }
}
