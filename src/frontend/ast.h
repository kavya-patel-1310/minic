#pragma once
#include "common/diagnostics.h"
#include "frontend/lexer.h"

#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

namespace mini {

struct Expr;
struct Stmt;
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;

enum class ExprKind {
    Int,          // 42, true, false
    Str,          // "text"
    Nil,          // nil
    Var,          // name
    Assign,       // name = value
    Unary,        // -x, !x
    Binary,       // a + b, a < b, ...
    Logical,      // a && b, a || b   (short-circuit)
    Call,         // f(args)
    Array,        // [a, b, c]
    Index,        // object[index]
    IndexAssign,  // object[index] = value
};

struct Expr {
    ExprKind kind;
    SourceLoc loc;
    int64_t intValue = 0;        // Int
    std::string str;             // Str contents, or the name in Var / Assign / Call
    Tok op = Tok::End;           // Unary / Binary / Logical operator
    ExprPtr left, right;         // operands; Index and IndexAssign use left[right]
    ExprPtr value;               // right-hand side of Assign / IndexAssign
    std::vector<ExprPtr> items;  // Call arguments / Array elements
};

enum class StmtKind { Let, ExprStmt, Print, If, While, For, Break, Continue, Block, Return, Fn };

struct Stmt {
    StmtKind kind;
    SourceLoc loc;
    std::string name;                 // Let variable, Fn name
    ExprPtr expr;                     // Let initializer, ExprStmt, If/While/For condition, Return value
    std::vector<ExprPtr> args;        // Print arguments
    StmtPtr thenBranch, elseBranch;   // If
    StmtPtr init;                     // For initializer (may be null)
    ExprPtr step;                     // For step (may be null)
    StmtPtr body;                     // While / For body
    std::vector<StmtPtr> stmts;       // Block / Fn body
    std::vector<std::string> params;  // Fn parameters
    bool echo = false;                // REPL: print the value of this expression statement
};

ExprPtr makeExpr(ExprKind kind, SourceLoc loc);
StmtPtr makeStmt(StmtKind kind, SourceLoc loc);
ExprPtr cloneExpr(const Expr& e);

// S-expression rendering, used by `minic ast` and the unit tests.
std::string exprToString(const Expr& e);
void printAst(const std::vector<StmtPtr>& program, std::ostream& out);

}  // namespace mini
