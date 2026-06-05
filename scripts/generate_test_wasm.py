#!/usr/bin/env python3
"""
Script to generate WASM test programs for function splitting.
This creates functions of various sizes to test the splitting pass.
"""

import sys
import os

def generate_large_function(name, num_instructions=1000):
    """Generate a function with many instructions."""
    lines = []
    lines.append(f"  (func ${name} (param $x i32) (result i32)")
    
    for i in range(num_instructions):
        lines.append(f"    (local.get $x)")
        lines.append(f"    (i32.const {i})")
        lines.append(f"    (i32.add)")
    
    lines.append("    (return)")
    lines.append("  )")
    return "\n".join(lines)

def generate_switch_function(name, num_cases=50):
    """Generate a function with a large switch statement."""
    lines = []
    lines.append(f"  (func ${name} (param $val i32) (result i32)")
    lines.append("    (block $switch_block (result i32)")
    lines.append("      (br_table $switch_block")
    
    # Generate case labels
    for i in range(num_cases):
        lines.append(f"        (i32.const {i})")
    
    # Default case
    lines.append("        (i32.const -1)")
    lines.append("      )")
    
    # Generate case bodies
    for i in range(num_cases):
        lines.append(f"      (i32.const {i * 2})")
        lines.append("      (return)")
    
    # Default body
    lines.append("      (i32.const -1)")
    lines.append("    )")
    lines.append("  )")
    return "\n".join(lines)

def generate_nested_function(name, depth=5, width=10):
    """Generate a function with nested blocks."""
    lines = []
    lines.append(f"  (func ${name} (param $x i32) (result i32)")
    
    # Create nested structure
    for d in range(depth):
        lines.append("    (block")
        for w in range(width):
            lines.append(f"      (local.get $x)")
            lines.append(f"      (i32.const {d * width + w})")
            lines.append(f"      (i32.add)")
    
    # Close all blocks
    for d in range(depth):
        lines.append("    )")
    
    lines.append("    (return)")
    lines.append("  )")
    return "\n".join(lines)

def generate_test_module(output_file, config=None):
    """Generate a complete WASM test module."""
    if config is None:
        config = {
            'large_func_size': 1500,
            'switch_cases': 100,
            'nested_depth': 3,
            'nested_width': 20
        }
    
    lines = []
    lines.append("(module")
    
    # Add large function
    lines.append(generate_large_function("large_func", config['large_func_size']))
    lines.append("")
    
    # Add switch function
    lines.append(generate_switch_function("switch_func", config['switch_cases']))
    lines.append("")
    
    # Add nested function
    lines.append(generate_nested_function("nested_func", config['nested_depth'], config['nested_width']))
    lines.append("")
    
    # Add small function (shouldn't be split)
    lines.append("  (func $small_func (param $a i32) (result i32)")
    lines.append("    (local.get $a)")
    lines.append("    (i32.const 1)")
    lines.append("    (i32.add)")
    lines.append("    (return)")
    lines.append("  )")
    
    lines.append(")")
    
    wasm_text = "\n".join(lines) + "\n"
    
    with open(output_file, 'w') as f:
        f.write(wasm_text)
    
    print(f"Generated test module: {output_file}")
    return output_file

def generate_simple_test(output_file):
    """Generate a simple test for basic functionality."""
    wasm_text = """(module
  (func $test_func (param $x i32) (result i32)
    (local.get $x)
    (i32.const 1)
    (i32.add)
    (return)
  )
)
"""
    with open(output_file, 'w') as f:
        f.write(wasm_text)
    print(f"Generated simple test: {output_file}")
    return output_file

def main():
    if len(sys.argv) < 2:
        print("Usage: python generate_test_wasm.py <output_file> [simple|large]")
        sys.exit(1)
    
    output_file = sys.argv[1]
    test_type = sys.argv[2] if len(sys.argv) > 2 else "large"
    
    if test_type == "simple":
        generate_simple_test(output_file)
    else:
        generate_test_module(output_file)

if __name__ == "__main__":
    main()