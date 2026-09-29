(module
  (import "env" "pair" (func $pair (result i32 i32)))
  (func (export "run") (result i32 i32)
    call $pair)
)
