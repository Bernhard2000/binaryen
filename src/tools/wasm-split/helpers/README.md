# WASM Instrumentation and Profile Data Extraction Guide

This guide explains how to instrument WebAssembly modules using Binaryen's `wasm-split` tool and extract profile data from different runtimes (Node.js, Wasmtime, Chicory, etc.).

## Table of Contents

1. [Overview](#overview)
2. [Instrumenting WASM Modules](#instrumenting-wasm-modules)
3. [Profile Data Format](#profile-data-format)
4. [Extracting Profile Data](#extracting-profile-data)
   - [Using Node.js](#using-nodejs)
   - [Using Wasmtime](#using-wasmtime)
   - [Using Chicory (Python)](#using-chicory-python)
   - [Using wasm-split Tool](#using-wasm-split-tool)
5. [Helper Scripts](#helper-scripts)
6. [Examples](#examples)
7. [API Reference](#api-reference)

## Overview

Binaryen's `wasm-split` tool provides instrumentation capabilities that allow you to:

- **Instrument** WASM modules to collect execution profiles
- **Split** modules based on profile data
- **Export** profile data in various formats (binary, JSON)

The instrumentation adds a `__write_profile` function that writes profile data to memory. This data includes:
- A module hash (for validation)
- Timestamps for each function (indicating execution order)

## Instrumenting WASM Modules

To instrument a WASM module, use the `--instrument` mode:

```bash
wasm-split input.wasm --instrument -o instrumented.wasm
```

### Instrumentation Options

- `--in-globals`: Store profile data in WebAssembly globals (default)
- `--in-memory`: Store profile data in the main memory (requires atomics)
- `--in-secondary-memory`: Store profile data in a separate memory (requires multimemory)
- `--profile-export <name>`: Name of the profile export function (default: `__write_profile`)
- `--secondary-memory-name <name>`: Name for secondary memory (default: `profile-data`)

### Example: Instrument with Secondary Memory

```bash
wasm-split input.wasm --instrument \
  --in-secondary-memory \
  --secondary-memory-name profile-data \
  -o instrumented.wasm
```

## Profile Data Format

The profile data has a simple binary format:

| Offset | Size | Description |
|--------|------|-------------|
| 0      | 8    | Module hash (64-bit, little-endian) |
| 8      | 4*N  | Function timestamps (32-bit each, little-endian) |

Where N is the number of defined functions in the module.

- **Module Hash**: A checksum of the original module, used to validate that the profile matches the module
- **Timestamps**: Non-zero values indicate the function was called. Lower values mean the function was called earlier in execution.

## Extracting Profile Data

### Using Node.js

Node.js provides direct access to WebAssembly memory, making it easy to extract profile data.

#### Method 1: Using the Helper Script

```bash
node helpers/get_profile_data_nodejs.mjs instrumented.wasm profile.json
```

#### Method 2: Manual Extraction

```javascript
const fs = require('fs');

async function extractProfile() {
  const wasmBuffer = fs.readFileSync('instrumented.wasm');
  const { instance } = await WebAssembly.instantiate(wasmBuffer);
  
  // Call __write_profile to write data to memory
  const profileSize = instance.exports.__write_profile(0, 65536);
  
  // Read profile data from memory
  const memory = instance.exports.memory || instance.exports['profile-memory'];
  const profileData = new Uint8Array(memory.buffer, 0, profileSize);
  
  // Parse the data (8-byte hash + 4-byte timestamps)
  const hashLow = new DataView(profileData.buffer).getUint32(0, true);
  const hashHigh = new DataView(profileData.buffer).getUint32(4, true);
  const moduleHash = (BigInt(hashHigh) << 32n) | BigInt(hashLow);
  
  const timestamps = [];
  for (let i = 8; i < profileSize; i += 4) {
    timestamps.push(new DataView(profileData.buffer).getUint32(i, true));
  }
  
  console.log('Module Hash:', moduleHash.toString(16));
  console.log('Timestamps:', timestamps);
}

extractProfile();
```

### Using Wasmtime

Wasmtime is a standalone WASM runtime. You can extract profile data using the CLI or programmatically.

#### Method 1: Using the Helper Script

```bash
chmod +x helpers/get_profile_data_wasmtime.sh
./helpers/get_profile_data_wasmtime.sh instrumented.wasm profile.json
```

#### Method 2: Using Wasmtime CLI

First, run your instrumented module to generate profile data, then use `wasm-split` to export it:

```bash
# Run your instrumented module (this generates profile data in memory)
wasmtime run --invoke __write_profile instrumented.wasm 0 65536

# Then use wasm-split to export as JSON
wasm-split instrumented.wasm --export-profile-json profile.json --profile profile.bin
```

### Using Chicory (Python)

Chicory is a WebAssembly runtime for Python.

#### Method 1: Using the Helper Script

```bash
python helpers/get_profile_data_chicory.py instrumented.wasm profile.json
```

#### Method 2: Manual Extraction

```python
from chicory import Chicory
import struct

# Load the module
engine = Chicory()
module = engine.load_module(open('instrumented.wasm', 'rb').read())

# Call __write_profile
profile_size = module.exports.__write_profile(0, 65536)

# Read profile data from memory
memory = module.exports.memory
profile_data = bytes(memory[0:profile_size])

# Parse the data
hash_low = struct.unpack_from('<I', profile_data, 0)[0]
hash_high = struct.unpack_from('<I', profile_data, 4)[0]
module_hash = (hash_high << 32) | hash_low

timestamps = []
offset = 8
while offset < len(profile_data):
    timestamps.append(struct.unpack_from('<I', profile_data, offset)[0])
    offset += 4

print(f"Module Hash: {module_hash:016x}")
print(f"Timestamps: {timestamps}")
```

### Using wasm-split Tool

The `wasm-split` tool provides built-in functionality to export profile data as JSON.

#### Export Profile as JSON

```bash
wasm-split instrumented.wasm --export-profile-json profile.json --profile profile.bin
```

This requires:
- `instrumented.wasm`: The original (uninstrumented) module
- `profile.bin`: The binary profile data generated by running the instrumented module

#### Print Human-Readable Profile

```bash
wasm-split instrumented.wasm --print-profile --profile profile.bin
```

## Helper Scripts

This directory contains helper scripts for different runtimes:

| Script | Runtime | Usage |
|--------|---------|-------|
| `get_profile_data_nodejs.mjs` | Node.js | `node get_profile_data_nodejs.mjs <wasm> <json>` |
| `get_profile_data_wasmtime.sh` | Wasmtime | `./get_profile_data_wasmtime.sh <wasm> <json>` |
| `get_profile_data_chicory.py` | Chicory | `python get_profile_data_chicory.py <wasm> <json>` |

All scripts:
1. Load the instrumented WASM module
2. Call `__write_profile` to write profile data to memory
3. Extract the profile data
4. Convert it to JSON format
5. Save to the specified file

## Examples

### Example 1: Full Workflow with Node.js

```bash
# Step 1: Instrument the module
wasm-split input.wasm --instrument -o instrumented.wasm

# Step 2: Run the instrumented module (this collects profile data)
node run_instrumented.js

# Step 3: Extract profile data
node helpers/get_profile_data_nodejs.mjs instrumented.wasm profile.json

# Step 4: Use profile data to split the module
wasm-split input.wasm --split --profile profile.bin -o primary.wasm --secondary secondary.wasm
```

### Example 2: Using JSON Export

```bash
# Instrument and run to generate profile.bin
# ... (run your instrumented module) ...

# Export profile as JSON
wasm-split input.wasm --export-profile-json profile.json --profile profile.bin

# View the JSON
cat profile.json
```

### Example 3: Custom Profile Export Name

```bash
# Instrument with custom export name
wasm-split input.wasm --instrument --profile-export my_profile_writer -o instrumented.wasm

# In your JavaScript:
const profileSize = instance.exports.my_profile_writer(0, 65536);
```

## API Reference

### `__write_profile(address: i32, size: i32) -> i32`

The main function exported by instrumented modules.

**Parameters:**
- `address`: The memory address to write profile data to
- `size`: The available size at that address

**Returns:**
- The actual size of the profile data written (in bytes)

**Behavior:**
- Writes profile data to the specified memory location
- Only writes if the available size is sufficient
- Returns 0 if there's insufficient space

### Profile Data Structure

```c
struct ProfileData {
  uint64_t module_hash;    // 8 bytes
  uint32_t timestamps[];   // 4 bytes per function
};
```

### JSON Format

When exported as JSON, the profile data has this structure:

```json
{
  "version": 1,
  "type": "wasm-split-profile",
  "moduleHash": "a1b2c3d4e5f67890",
  "functions": [
    {
      "name": "function_name",
      "timestamp": 12345
    },
    {
      "name": "another_function",
      "timestamp": 0
    }
  ]
}
```

- `version`: JSON format version (currently 1)
- `type`: Always "wasm-split-profile"
- `moduleHash`: Hexadecimal string of the module hash
- `functions`: Array of function information
  - `name`: Function name
  - `timestamp`: Execution timestamp (0 if not called)

## Tips and Best Practices

1. **Memory Size**: Ensure your WASM module has enough memory to hold the profile data. The profile size is `8 + (4 * num_functions)` bytes.

2. **Secondary Memory**: For multi-threaded applications, use `--in-secondary-memory` to avoid conflicts with main memory.

3. **Profile Validation**: Always validate that the module hash in the profile matches your module's hash before using it for splitting.

4. **Function Order**: The timestamps array corresponds to the order of functions in the module (as defined by `ModuleUtils::iterDefinedFunctions`).

5. **Zero Timestamps**: Functions with timestamp 0 were never called during the instrumented run.

## Troubleshooting

### Error: "Profile export not found"

**Cause**: The module wasn't instrumented or uses a custom export name.

**Solution**: 
- Verify the module was instrumented with `--instrument`
- Check if a custom `--profile-export` name was used
- Use `wasm-objdump -x instrumented.wasm` to list exports

### Error: "No memory export found"

**Cause**: The module doesn't export its memory.

**Solution**:
- Use `--in-globals` instead of memory-based storage
- Ensure your module exports memory
- Check for `profile-memory` export (created by wasm-split)

### Error: "Unexpected end of profile data"

**Cause**: The profile data is incomplete or corrupted.

**Solution**:
- Verify the profile was generated by running the instrumented module
- Check that the memory size is sufficient
- Ensure you're reading the correct amount of data

## Building Binaryen

To build Binaryen with WASM tools:

```bash
git clone https://github.com/WebAssembly/binaryen.git
cd binaryen
git submodule init
git submodule update
cmake . && make
```

The `wasm-split` tool will be in the `bin/` directory.

## License

This guide and the helper scripts are part of Binaryen and are licensed under the Apache License, Version 2.0.

## Contributing

Contributions to improve the helper scripts or this documentation are welcome! Please submit pull requests to the Binaryen repository.
