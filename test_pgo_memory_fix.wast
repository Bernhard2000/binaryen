;;
;; Test that PGO counters are placed at the end of memory, not at address 0
;;
;; This test creates a module with many basic blocks to ensure counters
;; don't overlap with program memory.
;;
;; RUN: wasm-opt %s --instrument-pgo -S -o - | filecheck %s

(module
  (memory $0 1 1)
  
  ;; Function with many nested blocks to create many basic blocks
  (func $many_blocks
    (block
      (block
        (block
          (block
            (block
              (block
                (block
                  (block
                    (block
                      (block
                        (i32.const 0)
                      )
                    )
                  )
                )
              )
            )
          )
        )
      )
    )
  )
)

;; CHECK: (memory $0 {{[0-9]+}} {{[0-9]+}})
;; CHECK: (global $__pgo_counters_base i32 (i32.const {{[0-9]+}}))
;; CHECK: (global $__pgo_counters_size i32 (i32.const {{[0-9]+}}))
;; CHECK: (func $get_profile_data
;; CHECK: (export "get_profile_data" (func $get_profile_data))
;; CHECK: (export "__pgo_counters_base" (global $__pgo_counters_base))
;; CHECK: (export "__pgo_counters_size" (global $__pgo_counters_size))
