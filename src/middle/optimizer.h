#pragma once
#include "frontend/ast.h"

namespace mini {

struct OptimizerStats {
    int constantsFolded = 0;   // expressions replaced by a literal
    int branchesRemoved = 0;   // if/while/for with a constant condition
    int deadStatements = 0;    // unreachable or side-effect-free statements removed
};

// AST-level optimizer, run between parsing and code generation:
//   * constant folding:    2 * 3 + 1  ->  7,   "a" + 1  ->  "a1",   !0  ->  1
//   * branch elimination:  if (0) {...} else B  ->  B,   while (false) {...}  ->  (removed)
//   * dead code removal:   statements after return/break/continue in a block
// The optimizer never changes observable behaviour: an operation that would fail at
// run time (such as division by zero) is left in place so the error still happens.
OptimizerStats optimize(std::vector<StmtPtr>& program);

}  // namespace mini
