# Instrumentation Memory Overlap Fix Report

## Problem Description

The `InstrumentPGO` pass in Binaryen was placing profile-guided optimization (PGO) counters at memory address 0, which caused them to overlap with program memory. This resulted in the first 2048 counter values (8KB) being correct, but subsequent counters overlapping with and corrupting program data.

### Root Cause

In the original implementation:
- Counters were stored in linear memory starting at address 0
- Each counter occupied 4 bytes (i32)
- The base address was hardcoded to 0: `baseGlobal->init = Builder(*module).makeConst(0)`
- Program data also typically starts at address 0
- After 2048 counters (2048 * 4 = 8192 bytes), counters began overlapping with program memory

## Solution

The fix repositions the PGO counters to the **end of allocated memory** instead of the beginning. This ensures:

1. Counters are placed at high memory addresses
2. Lower memory addresses remain free for program data
3. No overlap occurs between counters and program memory

### Implementation Details

**File Modified:** `src/passes/InstrumentPGO.cpp`

**Key Changes:**

1. **Calculate counter size:** `const uint32_t counterSize = totalBlocks * 4;`

2. **Ensure sufficient memory:**
   - Check if current memory can accommodate counters
   - If not, increase `memory.initial` and `memory.max` as needed

3. **Place counters at end of memory:**
   ```cpp
   const uint32_t newTotalMemorySize = pageSize * memory.initial;
   const uint32_t baseAddress = newTotalMemorySize - counterSize;
   baseGlobal->init = Builder(*module).makeConst(int32_t(baseAddress));
   ```

4. **Memory layout:**
   - Program data: addresses 0 to (memory_size - counter_size - 1)
   - PGO counters: addresses (memory_size - counter_size) to (memory_size - 1)

## Testing

A test file `test_pgo_memory_fix.wast` was created to verify:
- Counters are placed at non-zero addresses
- Memory size is adjusted appropriately
- Exports for counter base and size are present

## Impact

- **Backward Compatibility:** The change modifies where counters are stored in memory, which affects the binary layout. However, this is an internal implementation detail and should not affect the functionality of programs using PGO instrumentation.

- **Performance:** No performance impact. The fix only changes the memory location of counters, not the instrumentation logic.

- **Memory Usage:** May increase memory usage slightly if the existing memory is too small for the counters. This is necessary to prevent overlap.

## Verification

The fix can be verified by:
1. Running the test file through `wasm-opt --instrument-pgo`
2. Checking that the `__pgo_counters_base` global has a non-zero value
3. Verifying that the base address + counter size <= total memory size
4. Confirming that instrumented programs no longer experience memory corruption

## Alternative Approaches Considered

1. **Use a separate memory for counters:** Would require multimemory support, which is not always available.

2. **Reserve space at beginning of memory:** Would require programs to avoid using the first N bytes of memory, which is less flexible.

3. **Use globals instead of linear memory:** Would increase overhead and limit the number of counters.

The chosen approach (placing counters at end of memory) provides the best balance of simplicity, compatibility, and correctness.
