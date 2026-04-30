/**
 * Helper script to extract profile data from an instrumented WASM module in Node.js
 * 
 * Usage:
 *   node get_profile_data_nodejs.mjs <instrumented-wasm-file> <output-json-file>
 * 
 * This script:
 * 1. Loads the instrumented WASM module
 * 2. Calls the __write_profile function to write profile data to memory
 * 3. Extracts the profile data from memory
 * 4. Converts it to JSON format
 * 5. Saves it to the specified file
 */

import * as fs from 'fs';
import * as path from 'path';

const PROFILE_EXPORT = '__write_profile';
const PROFILE_MEMORY_EXPORT = 'profile-memory';

function getProfileDataFromWasm(wasmBuffer) {
  // Instantiate the WASM module
  const { instance } = await WebAssembly.instantiate(wasmBuffer);
  
  const exports = instance.exports;
  
  // Check if the profile export exists
  if (!exports[PROFILE_EXPORT]) {
    throw new Error(`Profile export '${PROFILE_EXPORT}' not found in WASM module`);
  }
  
  // Get the memory - try profile-memory first, then default memory
  let memory;
  if (exports[PROFILE_MEMORY_EXPORT]) {
    memory = exports[PROFILE_MEMORY_EXPORT];
  } else if (exports.memory) {
    memory = exports.memory;
  } else {
    throw new Error('No memory export found in WASM module');
  }
  
  // Call __write_profile to write profile data to memory
  // It takes (address, size) and returns the actual size written
  const buffer = memory.buffer;
  const profileSize = exports[PROFILE_EXPORT](0, buffer.byteLength);
  
  if (profileSize === 0) {
    throw new Error('Failed to write profile data');
  }
  
  // Read the profile data from memory
  const profileData = new Uint8Array(buffer, 0, profileSize);
  
  return {
    profileData,
    profileSize,
    memory: buffer
  };
}

function parseProfileData(profileData) {
  let offset = 0;
  
  // Read 8-byte hash (2 x 32-bit integers)
  const readUint32 = () => {
    if (offset + 4 > profileData.length) {
      throw new Error('Unexpected end of profile data');
    }
    const value = new DataView(profileData.buffer, profileData.byteOffset + offset, 4).getUint32(0, true);
    offset += 4;
    return value;
  };
  
  const hashLow = readUint32();
  const hashHigh = readUint32();
  const moduleHash = (BigInt(hashHigh) << 32n) | BigInt(hashLow);
  
  // Read timestamps (4 bytes each)
  const timestamps = [];
  while (offset < profileData.length) {
    timestamps.push(readUint32());
  }
  
  return {
    hash: moduleHash.toString(16).padStart(16, '0'),
    timestamps
  };
}

function profileDataToJson(profileData, functionNames = []) {
  const parsed = parseProfileData(profileData);
  
  const json = {
    version: 1,
    type: 'wasm-split-profile',
    moduleHash: parsed.hash,
    functions: []
  };
  
  for (let i = 0; i < parsed.timestamps.length; i++) {
    json.functions.push({
      name: functionNames[i] || `func_${i}`,
      timestamp: parsed.timestamps[i]
    });
  }
  
  return json;
}

// Main function
async function main() {
  const args = process.argv.slice(2);
  
  if (args.length < 2) {
    console.error('Usage: node get_profile_data_nodejs.mjs <instrumented-wasm-file> <output-json-file>');
    console.error('');
    console.error('Example:');
    console.error('  node get_profile_data_nodejs.mjs instrumented.wasm profile.json');
    process.exit(1);
  }
  
  const wasmFile = args[0];
  const outputFile = args[1];
  
  try {
    // Read the WASM file
    const wasmBuffer = fs.readFileSync(wasmFile);
    
    // Get profile data from WASM
    const { profileData, profileSize } = getProfileDataFromWasm(wasmBuffer);
    
    console.log(`Profile data size: ${profileSize} bytes`);
    console.log(`Number of function timestamps: ${(profileSize - 8) / 4}`);
    
    // Convert to JSON
    const json = profileDataToJson(profileData);
    
    // Write to output file
    fs.writeFileSync(outputFile, JSON.stringify(json, null, 2));
    
    console.log(`Profile data exported to: ${outputFile}`);
  } catch (error) {
    console.error('Error:', error.message);
    process.exit(1);
  }
}

main().catch(error => {
  console.error('Error:', error.message);
  process.exit(1);
});
