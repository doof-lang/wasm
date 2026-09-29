(module
  (memory (export "memory") 1)
  (func (export "spin")
    (loop $forever
      br $forever))
  (func (export "count") (param $n i32) (result i32)
    (local $i i32)
    (block $done
      (loop $next
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $next)))
    local.get $i)
  (func (export "grow") (param i32) (result i32)
    local.get 0
    memory.grow)
)
