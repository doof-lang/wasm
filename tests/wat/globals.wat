(module
  (global $counter (export "counter") (mut i32) (i32.const 5))
  (global (export "limit") i64 (i64.const 9))
  (func (export "counter_value") (result i32)
    global.get $counter)
)
