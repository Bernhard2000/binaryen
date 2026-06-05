// Test script to generate WASM programs for function splitting tests
// This script creates functions of various sizes to test the splitting pass

const fs = require('fs');

function generateWasmText() {
  let wasmText = `(module
`;

  // Generate a large function that should be split
  wasmText += `  (func $large_func (param $x i32) (result i32)
`;
  
  // Add many instructions to make it large
  for (let i = 0; i < 500; i++) {
    wasmText += `    (local.get $x)
`;
    wasmText += `    (i32.const ${i})
`;
    wasmText += `    (i32.add)
`;
  }
  
  wasmText += `    (return)
`;
  wasmText += `  )
`;

  // Generate a function with a large switch
  wasmText += `  (func $switch_func (param $val i32) (result i32)
`;
  wasmText += `    (block $switch_block (result i32)
`;
  wasmText += `      (br_table $switch_block
`;
  
  // Create a large switch with many cases
  for (let i = 0; i < 100; i++) {
    wasmText += `        (i32.const ${i})
`;
  }
  
  wasmText += `        (i32.const 0)
`; // default
  wasmText += `      )
`;
  
  // Add case bodies
  for (let i = 0; i < 100; i++) {
    wasmText += `      (i32.const ${i * 2})
`;
    wasmText += `      (return)
`;
  }
  
  wasmText += `      (i32.const -1)
`;
  wasmText += `    )
`;
  wasmText += `  )
`;

  // Generate a small function that shouldn't be split
  wasmText += `  (func $small_func (param $a i32) (result i32)
`;
  wasmText += `    (local.get $a)
`;
  wasmText += `    (i32.const 1)
`;
  wasmText += `    (i32.add)
`;
  wasmText += `    (return)
`;
  wasmText += `  )
`;

  wasmText += `)
`;

  return wasmText;
}

// Generate and save the test file
const wasmText = generateWasmText();
fs.writeFileSync('/workspace/Bernhard2000__binaryen/test/function_splitting_test.wat', wasmText);
console.log('Generated test file: function_splitting_test.wat');

// Also generate a simpler test for basic functionality
let simpleTest = `(module
  (func $test_func (param $x i32) (result i32)
    (local.get $x)
    (i32.const 1)
    (i32.add)
    (return)
  )
)
`;
fs.writeFileSync('/workspace/Bernhard2000__binaryen/test/simple_test.wat', simpleTest);
console.log('Generated simple test file: simple_test.wat');