/*
 * Copyright 2023 WebAssembly Community Group participants
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

#include <iostream>
#include <memory>
#include <string>
#include <sstream>

#include "analysis/cfg.h"
#include "parsing.h"
#include "pass.h"
#include "support/command-line.h"
#include "support/file.h"
#include "wasm-io.h"
#include "wasm-type.h"

using namespace wasm;

static std::string escape(std::string_view s) {
  std::string result;
  for (char c : s) {
    if (c == '"') {
      result += "\\\"";
    } else if (c == '\\') {
      result += "\\\\";
    } else if (c == '\n') {
      result += "\\n";
    } else {
      result += c;
    }
  }
  return result;
}

int main(int argc, const char* argv[]) {
  Options options("wasm-cfg", "Dump function CFGs in DOT format");
  options
      .add_positional("INFILE",
                      Options::Arguments::One,
                      [](Options* o, const std::string& argument) {
                        o->extra["infile"] = argument;
                      });
  options.parse(argc, argv);

  auto infile = options.extra["infile"];

  Module wasm;
  ModuleReader reader;
  try {
    reader.read(infile, wasm);
  } catch (ParseException& p) {
    p.dump(std::cerr);
    return 1;
  }

  std::cout << "digraph CFG {\n";
  std::cout << "  node [shape=box, fontname=courier, fontsize=10];\n";

  for (auto& func : wasm.functions) {
    if (func->imported()) {
      continue;
    }

    auto cfg = analysis::CFG::fromFunction(func.get());
    std::cout << "  subgraph \"cluster_" << escape(func->name.str) << "\" {\n";
    std::cout << "    label=\"" << escape(func->name.str) << "\";\n";

    for (const auto& block : cfg) {
      Index id = block.getIndex();
      // Using function name to ensure unique node names across functions
      // Format matches PGO instrumentation: function_name.block_index
      std::string blockId = std::string(func->name.str) + ".block_" + std::to_string(id);
      std::string nodeName = "f_" + std::string(func->name.str) + "_" + std::to_string(id);
      
      std::cout << "    \"" << escape(nodeName) << "\" [label=\"Block " << blockId << "\\n";
      for (auto* inst : block) {
        std::stringstream ss;
        ss << ShallowExpression{inst, &wasm};
        std::cout << escape(ss.str()) << "\\n";
      }
      std::cout << "\"];\n";

      for (const auto* succ : block.succs()) {
        std::string succNodeName = "f_" + std::string(func->name.str) + "_" + std::to_string(succ->getIndex());
        std::cout << "    \"" << escape(nodeName) << "\" -> \"" << escape(succNodeName) << "\";\n";
      }
    }
    std::cout << "  }\n";
  }

  std::cout << "}\n";

  return 0;
}
