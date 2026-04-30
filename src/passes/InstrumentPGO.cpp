/*
Copyright 2024 WebAssembly Community Group participants

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

//
// Instruments the build with low-overhead native PGO (Profile-Guided Optimization)
// counters. Instead of using FFI calls which are slow, this pass injects direct
// memory operations to increment counters in linear memory.
//
// The pass:
// 1. Allocates a block of linear memory for counters (N * 4 bytes for N basic blocks)
// 2. Injects increment operations at the start of each basic block
// 3. Exports a helper function to retrieve the profile data
//
// This provides ultra-low-overhead profiling suitable for cost-model-driven
// optimizations like --outlining.
//

#include "shared-constants.h"
#include <pass.h>
#include <wasm-builder.h>
#include <wasm.h>

namespace wasm {

static Name PGO_COUNTERS_BASE("__pgo_counters_base");
static Name PGO_COUNTERS_SIZE("__pgo_counters_size");
static Name GET_PROFILE_DATA("get_profile_data");

struct InstrumentPGO : public WalkerPass<PostWalker<InstrumentPGO>> {
  // Adds memory operations.
  bool addsEffects() override { return true; }

  Index nextBlockId = 0;
  Index totalBlocks = 0;
  Name memoryName;

  void run(Module* module) override {
    // Ensure we have a memory
    if (module->memories.empty()) {
      auto memory = std::make_unique<Memory>();
      memory->name = "memory";
      memory->initial = 1; // 1 page (64KB)
      module->addMemory(std::move(memory));
    }

    memoryName = module->memories[0]->name;

    // First pass: count blocks
    Super::run(module);

    // Now that we've counted blocks, add the globals and export
    // Create a global to hold the base address of the counters
    auto baseGlobal = std::make_unique<Global>();
    baseGlobal->name = PGO_COUNTERS_BASE;
    baseGlobal->type = Type::i32;
    baseGlobal->init = Builder(*module).makeConst(0);
    baseGlobal->mutable_ = true;
    module->addGlobal(std::move(baseGlobal));

    // Create a global to hold the size of the counters
    auto sizeGlobal = std::make_unique<Global>();
    sizeGlobal->name = PGO_COUNTERS_SIZE;
    sizeGlobal->type = Type::i32;
    sizeGlobal->init = Builder(*module).makeConst(int32_t(totalBlocks * 4));
    sizeGlobal->mutable_ = true;
    module->addGlobal(std::move(sizeGlobal));

    // Create the get_profile_data export function
    // Signature: () -> i32
    auto func = std::make_unique<Function>();
    func->name = GET_PROFILE_DATA;
    func->type = Type(Signature(Type::none, Type::i32), NonNullable, Exact);

    Builder builder(*module);
    func->body = builder.makeGlobalGet(PGO_COUNTERS_BASE, Type::i32);

    module->addFunction(std::move(func));

    // Add export
    auto export_ = Builder::makeExport(GET_PROFILE_DATA, GET_PROFILE_DATA, ExternalKind::Function);
    module->addExport(std::move(export_));
  }

  void visitFunction(Function* curr) {
    if (curr->imported()) return;

    // Instrument function entry
    Index blockId = nextBlockId++;
    totalBlocks++;
    Builder builder(*getModule());
    Expression* increment = makeCounterIncrement(builder, blockId);

    if (curr->body) {
      curr->body = builder.makeSequence(increment, curr->body);
    } else {
      curr->body = increment;
    }

    Super::visitFunction(curr);
  }

  void visitBlock(Block* curr) {
    // Skip empty blocks that are just control flow structure
    if (curr->list.size() > 0 || curr->name != Name()) {
      // Instrument this block
      Index blockId = nextBlockId++;
      totalBlocks++;
      Builder builder(*getModule());
      Expression* increment = makeCounterIncrement(builder, blockId);

      // Insert at the beginning
      curr->list.insertAt(0, increment);
    }

    Super::visitBlock(curr);
  }

  void visitLoop(Loop* curr) {
    // Instrument loop header
    Index blockId = nextBlockId++;
    totalBlocks++;
    Builder builder(*getModule());
    Expression* increment = makeCounterIncrement(builder, blockId);

    if (curr->body) {
      curr->body = builder.makeSequence(increment, curr->body);
    } else {
      curr->body = increment;
    }

    Super::visitLoop(curr);
  }

  void visitIf(If* curr) {
    // Instrument then branch
    if (curr->ifTrue) {
      Index thenId = nextBlockId++;
      totalBlocks++;
      Builder builder(*getModule());
      Expression* increment = makeCounterIncrement(builder, thenId);
      curr->ifTrue = builder.makeSequence(increment, curr->ifTrue);
    }

    // Instrument else branch
    if (curr->ifFalse) {
      Index elseId = nextBlockId++;
      totalBlocks++;
      Builder builder(*getModule());
      Expression* increment = makeCounterIncrement(builder, elseId);
      curr->ifFalse = builder.makeSequence(increment, curr->ifFalse);
    }

    Super::visitIf(curr);
  }

private:
  Expression* makeCounterIncrement(Builder& builder, Index blockId) {
    // Generate code to increment counter at offset blockId*4 from base
    // (i32.store
    //   (i32.add (i32.const BASE) (i32.const blockId*4))
    //   (i32.add
    //     (i32.load (i32.add (i32.const BASE) (i32.const blockId*4)))
    //     (i32.const 1)
    //   )
    // )

    Address offset = blockId * 4;

    // Get base address from global
    Expression* base = builder.makeGlobalGet(PGO_COUNTERS_BASE, Type::i32);

    // Calculate counter address: base + offset
    Expression* counterAddr = builder.makeBinary(
      AddInt32,
      base,
      builder.makeConst(int32_t(offset))
    );

    // Load current value
    Expression* currentValue = builder.makeLoad(
      4, false, 0, 4, counterAddr, Type::i32, memoryName
    );

    // Add 1
    Expression* newValue = builder.makeBinary(
      AddInt32,
      currentValue,
      builder.makeConst(int32_t(1))
    );

    // Store back
    return builder.makeStore(
      4, 0, 4, counterAddr, newValue, Type::i32, memoryName
    );
  }
};

Pass* createInstrumentPGOPass() { return new InstrumentPGO(); }

} // namespace wasm
