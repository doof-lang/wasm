;; wasi-threads module in the shape wasi-libc toolchains produce: imported
;; shared memory, "wasi"."thread-spawn", and an exported wasi_thread_start.
;;
;; Memory layout: 0 = sum of worker arguments, 4 = finished workers,
;; 8 = mode (0 = add, 1 = call env.note first, 2 = spin forever, 3 = wait forever).
(module
  (import "env" "memory" (memory 1 1 shared))
  (import "wasi" "thread-spawn" (func $spawn (param i32) (result i32)))
  (import "env" "note" (func $note (param i32)))

  (func (export "wasi_thread_start") (param $tid i32) (param $arg i32)
    (local $mode i32)
    (local.set $mode (i32.atomic.load (i32.const 8)))
    (if (i32.eq (local.get $mode) (i32.const 1))
      (then (call $note (local.get $arg))))
    (if (i32.eq (local.get $mode) (i32.const 2))
      (then (loop $spin (br $spin))))
    (if (i32.eq (local.get $mode) (i32.const 3))
      (then (drop (memory.atomic.wait32 (i32.const 12) (i32.const 0) (i64.const -1)))))
    (drop (i32.atomic.rmw.add (i32.const 0) (local.get $arg)))
    (drop (i32.atomic.rmw.add (i32.const 4) (i32.const 1)))
    (drop (memory.atomic.notify (i32.const 4) (i32.const -1))))

  ;; Spawns `count` workers with arguments 1..count; returns -1 if a spawn fails.
  (func $spawnWorkers (param $count i32) (param $mode i32) (result i32)
    (local $i i32)
    (i32.atomic.store (i32.const 0) (i32.const 0))
    (i32.atomic.store (i32.const 4) (i32.const 0))
    (i32.atomic.store (i32.const 8) (local.get $mode))
    (block $spawned
      (loop $spawning
        (br_if $spawned (i32.ge_s (local.get $i) (local.get $count)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (if (i32.lt_s (call $spawn (local.get $i)) (i32.const 0))
          (then (return (i32.const -1))))
        (br $spawning)))
    (i32.const 0))

  ;; Runs `count` workers to completion and returns the sum of their arguments.
  (func (export "run") (param $count i32) (param $mode i32) (result i32)
    (local $done i32)
    (if (i32.lt_s (call $spawnWorkers (local.get $count) (local.get $mode)) (i32.const 0))
      (then (return (i32.const -1))))
    (block $finished
      (loop $waiting
        (local.set $done (i32.atomic.load (i32.const 4)))
        (br_if $finished (i32.ge_s (local.get $done) (local.get $count)))
        (drop (memory.atomic.wait32 (i32.const 4) (local.get $done) (i64.const -1)))
        (br $waiting)))
    (i32.atomic.load (i32.const 0)))

  ;; Starts workers and returns without waiting for them.
  (func (export "detach") (param $count i32) (param $mode i32) (result i32)
    (call $spawnWorkers (local.get $count) (local.get $mode)))

  (func (export "note") (param i32)
    (call $note (local.get 0)))
)
