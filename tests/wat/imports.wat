(module
  (import "env" "multiply" (func $multiply (param i32 i32) (result i32)))
  (func (export "run") (param i32) (result i32)
    local.get 0
    i32.const 2
    call $multiply)
)
