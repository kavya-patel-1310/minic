#pragma once
#include "common/diagnostics.h"
#include "frontend/ast.h"

#include <ostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace mini {

// Native backend: translates the AST straight to x86-64 assembly (GNU as, AT&T
// syntax, System V ABI). The output links against libc with `cc`.
//
// The native backend compiles the statically typed subset of MiniLang: integer
// variables and arithmetic, control flow, functions (up to 6 parameters) and
// recursion, with string literals allowed as `print` arguments. Programs that use
// arrays, string operations, nil or built-ins are rejected with a clear error and
// can still be run on the VM.
//
// Code generation strategy: every expression leaves its result in %rax; binary
// operators spill the left operand to the machine stack. Locals live in fixed
// %rbp-relative slots. The generator tracks the number of pending pushes so
// that every call is made with a 16-byte aligned stack, as the ABI requires.
class X86Codegen {
public:
    explicit X86Codegen(Diagnostics& diag) : diag_(diag) {}

    // Returns false if errors were reported.
    bool generate(const std::vector<StmtPtr>& program, std::ostream& out);

private:
    struct Loop {
        std::string breakLabel;
        std::string continueLabel;
    };

    Diagnostics& diag_;
    std::ostringstream* code_ = nullptr;  // body of the function being generated
    std::unordered_map<std::string, int> arity_;
    std::unordered_map<std::string, bool> globals_;
    std::vector<std::string> strings_;
    std::vector<std::unordered_map<std::string, int>> scopes_;
    std::vector<Loop> loops_;
    int nextSlot_ = 0;
    int maxSlot_ = 0;
    int depth_ = 0;  // 8-byte values pushed by expression evaluation
    int labels_ = 0;
    bool inFunction_ = false;

    void ins(const std::string& text);
    void label(const std::string& name);
    std::string newLabel();
    void push();
    void pop(const char* reg);
    void callAligned(const std::string& symbol, bool varargs);
    std::string slotAddr(int slot) const;
    std::string stringLabel(const std::string& s);
    int declareLocal(const std::string& name, SourceLoc loc);
    std::string varAddr(const std::string& name, SourceLoc loc);

    void function(const Stmt& fn, std::ostream& out);
    void mainFunction(const std::vector<StmtPtr>& program, std::ostream& out);
    void emitFunction(const std::string& symbol, const std::vector<std::string>& params,
                      const std::string& body, std::ostream& out, bool returnsZero);
    void scoped(const Stmt& s);
    void stmt(const Stmt& s);
    void print(const Stmt& s);
    void expr(const Expr& e);
    void binary(const Expr& e);
    void operands(const Expr& e);                         // left -> %rax, right -> %rcx
    bool simpleOperand(const Expr& e, std::string& src);  // literal or variable usable directly
    void jumpIfFalse(const Expr& cond, const std::string& target);
    [[noreturn]] void unsupported(const std::string& what, SourceLoc loc);
};

}  // namespace mini
