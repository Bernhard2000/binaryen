/*
 * Copyright 2015 WebAssembly Community Group participants
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ir/module-utils.h"
#include "pass.h"
#include "wasm-builder.h"
#include "wasm.h"

namespace wasm {

// Name for the profile data global
static const Name PROFILE_DATA_GLOBAL("__profile_data");
static const Name PROFILE_DATA_SIZE_GLOBAL("__profile_data_size");
static const Name GET_PROFILE_DATA_FUNCTION("get_profile_data");

class InstrumentPGO : public Pass {
public:
  InstrumentPGO() : Pass("instrument-pgo", "InstrumentPGO", "Add PGO instrumentation") {}

  void run(Module* module) override {
    // Create the profile data memory
    ensureProfileDataGlobal(module);
    
    // Instrument each function
    for (auto& func : module->functions) {
      if (func->imported) continue;
      instrumentFunction(func.get(), module);
    }
    
    // Export the get_profile_data function
    ensureGetProfileDataFunction(module);
  }

private:
  void ensureProfileDataGlobal(Module* module) {
    // Check if the global already exists
    if (module->getGlobalOrNull(PROFILE_DATA_GLOBAL)) return;
    
    // Create a global for the profile data (i32 pointer to memory)
    auto* profileDataGlobal = new Global();
    profileDataGlobal->name = PROFILE_DATA_GLOBAL;
    profileDataGlobal->type = Type(i32);
    profileDataGlobal->init = Builder(*module).makeI32(0);
    profileDataGlobal->mutable_ = true;
    module->addGlobal(profileDataGlobal);
    
    // Create the profile data size global
    auto* profileDataSizeGlobal = new Global();
    profileDataSizeGlobal->name = PROFILE_DATA_SIZE_GLOBAL;
    profileDataSizeGlobal->type = Type(i32);
    profileDataSizeGlobal->init = Builder(*module).makeI32(0);
    profileDataSizeGlobal->mutable_ = true;
    module->addGlobal(profileDataSizeGlobal);
  }

  void instrumentFunction(Function* func, Module* module) {
    if (func->body.isNull()) return;
    
    // Count the number of blocks that need instrumentation
    std::vector<Expression*> blocksToInstrument;
    
    // Walk through the function body to find all blocks
    struct BlockCounter : public PostWalker<BlockCounter> {
      std::vector<Expression*>& blocks;
      BlockCounter(std::vector<Expression*>& blocks) : blocks(blocks) {}
      
      void visitBlock(Block* curr) {
        blocks.push_back(curr);
      }
      
      void visitLoop(Loop* curr) {
        blocks.push_back(curr);
      }
      
      void visitIf(If* curr) {
        blocks.push_back(curr);
      }
    };
    
    BlockCounter counter(blocksToInstrument);
    counter.walk(func->body);
    
    // If no blocks to instrument, return
    if (blocksToInstrument.empty()) return;
    
    // Allocate space for counters (one i32 per block)
    Builder builder(*module);
    
    // Get the current size
    auto* sizeGlobal = module->getGlobal(PROFILE_DATA_SIZE_GLOBAL);
    auto currentSize = builder.makeGlobalGet(sizeGlobal->name, sizeGlobal->type);
    
    // Calculate new size (current size + number of counters)
    auto numCounters = builder.makeI32(blocksToInstrument.size());
    auto newSize = builder.makeBinop(AddInt32, currentSize, numCounters);
    
    // Update the size global
    builder.makeGlobalSet(sizeGlobal->name, newSize);
    
    // Get the profile data pointer
    auto* dataGlobal = module->getGlobal(PROFILE_DATA_GLOBAL);
    auto dataPtr = builder.makeGlobalGet(dataGlobal->name, dataGlobal->type);
    
    // For each block, inject a counter increment at the beginning
    Index counterIndex = 0;
    for (auto* block : blocksToInstrument) {
      // Calculate the address for this counter
      auto counterOffset = builder.makeI32(counterIndex * 4); // 4 bytes per i32
      auto counterAddr = builder.makeBinop(AddInt32, dataPtr, counterOffset);
      
      // Load the current counter value
      auto currentValue = builder.makeLoad(4, 0, counterAddr, i32, "pgo_counter_load");
      
      // Increment the counter
      auto incrementedValue = builder.makeBinop(AddInt32, currentValue, builder.makeI32(1));
      
      // Store the incremented value back
      auto store = builder.makeStore(4, 0, counterAddr, incrementedValue, i32, "pgo_counter_store");
      
      // Insert the instrumentation at the beginning of the block
      if (auto* blockExpr = block->dynCast<Block>()) {
        blockExpr->list.insert(blockExpr->list.begin(), store);
      } else if (auto* loopExpr = block->dynCast<Loop>()) {
        loopExpr->body.insert(loopExpr->body.begin(), store);
      } else if (auto* ifExpr = block->dynCast<If>()) {
        // For if, we need to handle both branches
        // Insert before the condition
        // This is a bit tricky - we'll insert in the true branch
        if (!ifExpr->ifTrue.isNull()) {
          if (auto* trueBlock = ifExpr->ifTrue.dynCast<Block>()) {
            trueBlock->list.insert(trueBlock->list.begin(), store);
          }
        }
      }
      
      counterIndex++;
    }
  }

  void ensureGetProfileDataFunction(Module* module) {
    // Check if the function already exists
    if (module->getFunctionOrNull(GET_PROFILE_DATA_FUNCTION)) return;
    
    // Create the get_profile_data function
    // Signature: () -> i32 (returns pointer to profile data)
    Function* func = new Function();
    func->name = GET_PROFILE_DATA_FUNCTION;
    func->result = i32;
    
    // Get the profile data global
    auto* dataGlobal = module->getGlobal(PROFILE_DATA_GLOBAL);
    if (!dataGlobal) {
      ensureProfileDataGlobal(module);
      dataGlobal = module->getGlobal(PROFILE_DATA_GLOBAL);
    }
    
    // Function body: return the profile data pointer
    Builder builder(*module);
    func->body = builder.makeGlobalGet(dataGlobal->name, dataGlobal->type);
    
    module->addFunction(func);
    
    // Export the function
    auto* exportFunc = new Export();
    exportFunc->name = GET_PROFILE_DATA_FUNCTION;
    exportFunc->value = func->name;
    module->addExport(exportFunc);
  }
};

Pass* createInstrumentPGOPass() {
  return new InstrumentPGO();
}

} // namespace wasm
