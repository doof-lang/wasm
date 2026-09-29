;; One single-result host import per numeric type.
(module
  (import "env" "i32" (func $i32 (param i32) (result i32)))
  (import "env" "i64" (func $i64 (param i64) (result i64)))
  (import "env" "f32" (func $f32 (param f32) (result f32)))
  (import "env" "f64" (func $f64 (param f64) (result f64)))
  (func (export "run") (param i32 i64 f32 f64) (result i32 i64 f32 f64)
    local.get 0
    call $i32
    local.get 1
    call $i64
    local.get 2
    call $f32
    local.get 3
    call $f64)
)
