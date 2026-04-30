#!/usr/bin/env python3
"""
Helper script to extract profile data from an instrumented WASM module using Chicory

Usage:
    python get_profile_data_chicory.py <instrumented-wasm-file> <output-json-file>

This script:
1. Loads the instrumented WASM module using Chicory
2. Calls the __write_profile function to write profile data to memory
3. Extracts the profile data from memory
4. Converts it to JSON format
5. Saves it to the specified file

Requirements:
    - chicory must be installed (pip install chicory)
"""

import sys
import struct
import json

PROFILE_EXPORT = '__write_profile'
PROFILE_MEMORY_EXPORT = 'profile-memory'

def get_profile_data_from_wasm(wasm_bytes):
    """Extract profile data from an instrumented WASM module."""
    try:
        from chicory import Chicory
    except ImportError:
        print("Error: chicory module not found. Please install it with: pip install chicory")
        sys.exit(1)
    
    # Load the WASM module
    engine = Chicory()
    module = engine.load_module(wasm_bytes)
    
    # Get the exports
    exports = module.exports
    
    # Check if the profile export exists
    if PROFILE_EXPORT not in exports:
        raise ValueError(f"Profile export '{PROFILE_EXPORT}' not found in WASM module")
    
    # Get the memory
    memory = None
    if PROFILE_MEMORY_EXPORT in exports:
        memory = exports[PROFILE_MEMORY_EXPORT]
    elif 'memory' in exports:
        memory = exports['memory']
    else:
        raise ValueError('No memory export found in WASM module')
    
    # Call __write_profile to write profile data to memory
    # It takes (address, size) and returns the actual size written
    write_profile = exports[PROFILE_EXPORT]
    profile_size = write_profile(0, len(memory) - 1024)  # Use most of memory
    
    if profile_size == 0:
        raise ValueError('Failed to write profile data')
    
    # Read the profile data from memory
    profile_data = bytes(memory[0:profile_size])
    
    return {
        'profile_data': profile_data,
        'profile_size': profile_size,
        'memory': memory
    }

def parse_profile_data(profile_data):
    """Parse binary profile data into a structured format."""
    offset = 0
    
    # Read 8-byte hash (2 x 32-bit integers, little-endian)
    if len(profile_data) < 8:
        raise ValueError('Profile data too short for hash')
    
    hash_low = struct.unpack_from('<I', profile_data, offset)[0]
    offset += 4
    hash_high = struct.unpack_from('<I', profile_data, offset)[0]
    offset += 4
    module_hash = (hash_high << 32) | hash_low
    
    # Read timestamps (4 bytes each, little-endian)
    timestamps = []
    while offset < len(profile_data):
        if offset + 4 > len(profile_data):
            raise ValueError('Unexpected end of profile data')
        timestamp = struct.unpack_from('<I', profile_data, offset)[0]
        timestamps.append(timestamp)
        offset += 4
    
    return {
        'hash': f"{module_hash:016x}",
        'timestamps': timestamps
    }

def profile_data_to_json(profile_data, function_names=None):
    """Convert profile data to JSON format."""
    parsed = parse_profile_data(profile_data)
    
    json_data = {
        'version': 1,
        'type': 'wasm-split-profile',
        'moduleHash': parsed['hash'],
        'functions': []
    }
    
    if function_names is None:
        function_names = []
    
    for i, timestamp in enumerate(parsed['timestamps']):
        func_name = function_names[i] if i < len(function_names) else f'func_{i}'
        json_data['functions'].append({
            'name': func_name,
            'timestamp': timestamp
        })
    
    return json_data

def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <instrumented-wasm-file> <output-json-file>")
        print()
        print("Example:")
        print(f"  {sys.argv[0]} instrumented.wasm profile.json")
        sys.exit(1)
    
    wasm_file = sys.argv[1]
    output_file = sys.argv[2]
    
    try:
        # Read the WASM file
        with open(wasm_file, 'rb') as f:
            wasm_bytes = f.read()
        
        # Get profile data from WASM
        result = get_profile_data_from_wasm(wasm_bytes)
        profile_data = result['profile_data']
        profile_size = result['profile_size']
        
        print(f"Profile data size: {profile_size} bytes")
        print(f"Number of function timestamps: {(profile_size - 8) // 4}")
        
        # Convert to JSON
        json_data = profile_data_to_json(profile_data)
        
        # Write to output file
        with open(output_file, 'w') as f:
            json.dump(json_data, f, indent=2)
        
        print(f"Profile data exported to: {output_file}")
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)

if __name__ == '__main__':
    main()
