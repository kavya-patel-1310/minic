#pragma once
#include "common/diagnostics.h"
#include "frontend/ast.h"
#include "middle/optimizer.h"

#include <istream>
#include <ostream>
#include <string>
#include <vector>

namespace mini {

constexpr const char* kVersion = "1.0.0";

// Exit codes used by the command-line tool.
enum ExitCode : int { kOk = 0, kCompileError = 1, kRuntimeError = 2, kUsage = 64, kIoError = 66, kToolError = 70 };

// Lex + parse (+ optimize). Errors go to `diag`; returns false if there were any.
bool runFrontend(const std::string& source, Diagnostics& diag, bool optimize, std::vector<StmtPtr>& ast,
                 OptimizerStats* stats = nullptr);

// Full pipeline on the VM: compile `source` and execute it.
// Program output goes to `out`; diagnostics and runtime errors go to `err`.
int runSource(const std::string& filename, const std::string& source, bool optimize, std::ostream& out,
              std::istream& in, std::ostream& err, bool color = false);

// Native pipeline: compile `source` to x86-64 assembly text.
bool compileToAssembly(const std::string& filename, const std::string& source, bool optimize, std::string& asmOut,
                       std::ostream& err, bool color = false);

bool readFile(const std::string& path, std::string& contents);

}  // namespace mini
