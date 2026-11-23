#pragma once
#include "backend/bytecode.h"
#include "common/diagnostics.h"
#include "frontend/ast.h"

#include <unordered_map>

namespace mini {

// Lowers the AST to stack-machine bytecode and performs semantic checks
// (name resolution, arity, break/continue placement, ...).
//
// Scoping rules:
//   * `let` at the top level of a script declares a global, visible in every function.
//   * Any other `let` declares a block-scoped local stored in a numbered stack slot.
//   * Functions may be called before their definition.
//
// The compiler is incremental: each call to compile() appends a new entry
// function to the same Program, which is what the REPL uses. A chunk that fails
// to compile is rolled back completely.
class Compiler {
public:
    explicit Compiler(Diagnostics* diag) : diag_(diag) {}
    void setDiagnostics(Diagnostics* diag) { diag_ = diag; }

    // Returns the index of the chunk's entry function, or -1 if errors were reported.
    int compile(const std::vector<StmtPtr>& program, const std::string& name = "<script>");
    const Program& program() const { return program_; }

private:
    struct Loop {
        std::vector<std::size_t> breaks;
        std::vector<std::size_t> continues;
    };
    friend struct ScopeGuard;
    friend struct LoopGuard;

    Diagnostics* diag_;
    Program program_;
    std::unordered_map<std::string, int> fnIndex_;
    std::unordered_map<std::string, int> globalIndex_;
    std::unordered_map<int64_t, int> intConsts_;
    std::unordered_map<std::string, int> strConsts_;

    Function* cur_ = nullptr;
    std::vector<std::unordered_map<std::string, int>> scopes_;  // empty => top level of the script
    std::vector<Loop> loops_;
    int nextSlot_ = 0;
    int depth_ = 0;  // simulated operand-stack depth, for Function::maxStack

    void emit(int32_t word, SourceLoc loc);
    void adjustDepth(int delta);
    void trackStack(Op op, int32_t operand, int32_t operand2);
    void emitOp(Op op, SourceLoc loc) {
        trackStack(op, 0, 0);
        emit(op, loc);
    }
    void emitOp(Op op, int32_t operand, SourceLoc loc) {
        trackStack(op, operand, 0);
        emit(op, loc);
        emit(operand, loc);
    }
    std::size_t emitJump(Op op, SourceLoc loc);
    void patchJump(std::size_t at, std::size_t target);
    std::size_t here() const { return cur_->code.size(); }

    int constInt(int64_t v);
    int constStr(const std::string& s);
    int declareGlobal(const std::string& name);
    int declareLocal(const std::string& name, SourceLoc loc);
    int resolveLocal(const std::string& name) const;

    void compileFunction(const Stmt& fn, int index);
    void stmtList(const std::vector<StmtPtr>& stmts);
    void scopedStmt(const Stmt& s);
    void stmt(const Stmt& s);
    void expr(const Expr& e);
    void call(const Expr& e);
};

}  // namespace mini
