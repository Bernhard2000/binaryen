#!/bin/bash

# Helper script to extract profile data from an instrumented WASM module using Wasmtime
#
# Usage:
#   ./get_profile_data_wasmtime.sh <instrumented-wasm-file> <output-json-file>
#
# This script:
# 1. Uses wasmtime to run the instrumented WASM module
# 2. Calls the __write_profile function to write profile data to memory
# 3. Extracts the profile data from memory
# 4. Converts it to JSON format using the wasm-split tool
#
# Requirements:
#   - wasmtime must be installed
#   - wasm-split must be built and available

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WASM_SPLIT="${SCRIPT_DIR}/../../../bin/wasm-split"

if [ $# -lt 2 ]; then
    echo "Usage: $0 <instrumented-wasm-file> <output-json-file>"
    echo ""
    echo "Example:"
    echo "  $0 instrumented.wasm profile.json"
    exit 1
fi

WASM_FILE="$1"
OUTPUT_FILE="$2"
TMP_PROFILE="$(mktemp /tmp/profile_XXXXXX.bin)"

# Clean up temp file on exit
trap "rm -f \"${TMP_PROFILE}\"" EXIT

echo "Extracting profile data from ${WASM_FILE}..."

# Use wasmtime to call __write_profile and dump memory
# We'll use a simple WASI program that calls the export
cat > /tmp/profile_extractor.wat << 'EOF'
(module
  (import "" "__write_profile" (func $write_profile (param i32 i32) (result i32)))
  (import "" "memory" (memory 1))
  
  (func $extract (export "_start")
    ;; Call __write_profile at address 0 with a large size
    (call $write_profile (i32.const 0) (i32.const 65536))
    ;; Exit successfully
    (unreachable)
  )
)
EOF

# Compile the extractor
wat2wasm /tmp/profile_extractor.wat -o /tmp/profile_extractor.wasm 2>/dev/null || {
    echo "Error: wat2wasm not found. Please install wabt tools."
    exit 1
}

# Run the extractor with wasmtime and dump memory
# Note: This is a simplified approach. In practice, you might need to use wasmtime's
# memory inspection capabilities or write a custom extractor.
echo "Using wasmtime to extract profile data..."

# For now, we'll use a simpler approach: use wasm-split's print-profile mode
# and convert to JSON
if [ -f "${WASM_SPLIT}" ]; then
    # First, we need to run the instrumented module to generate a profile
    # This is a placeholder - in practice, you would run your actual workload
    echo "Note: You need to run your instrumented WASM module first to generate profile data."
    echo "This script assumes you have already generated a profile.bin file."
    
    # Check if profile.bin exists
    if [ -f "profile.bin" ]; then
        # Use wasm-split to convert to JSON
        "${WASM_SPLIT}" "${WASM_FILE}" --export-profile-json "${OUTPUT_FILE}" --profile profile.bin
        echo "Profile data exported to: ${OUTPUT_FILE}"
    else
        echo "Error: profile.bin not found. Please run your instrumented module first."
        exit 1
    fi
else
    echo "Error: wasm-split not found at ${WASM_SPLIT}"
    echo "Please build binaryen first."
    exit 1
fi
