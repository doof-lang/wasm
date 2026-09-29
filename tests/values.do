// Test helpers that narrow WasmValue results, failing the test on a type mismatch.

import { Assert } from "std/assert"
import { WasmValue } from "../index"

export function intValue(value: WasmValue): int {
  result := value as int else { Assert.fail("expected an i32 value"); return 0 }
  return result
}

export function longValue(value: WasmValue): long {
  result := value as long else { Assert.fail("expected an i64 value"); return 0L }
  return result
}
