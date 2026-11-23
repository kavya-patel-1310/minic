#pragma once
#include "common/diagnostics.h"
#include "frontend/ast.h"

#include <initializer_list>

namespace mini {

// Recursive-descent parser. Precedence, lowest to highest:
//
//   assignment   =  +=  -=  *=  /=  %=      (right associative)
//   or           ||
//   and          &&
//   equality     ==  !=
//   comparison   <  <=  >  >=
//   term         +  -
//   factor       *  /  %
//   unary        -  !
//   postfix      a[i]
//   primary      literals, names, calls, arrays, ( expr )
//
// Syntax errors are recorded in Diagnostics; the parser then resynchronises at
// the next statement so that several errors can be reported in one run.
class Parser {
public:
    Parser(std::vector<Token> tokens, Diagnostics& diag);
    std::vector<StmtPtr> parseProgram();

private:
    std::vector<Token> t_;
    std::size_t p_ = 0;
    Diagnostics& diag_;
    bool recovering_ = false;  // suppress cascading errors right after a syntax error

    const Token& peek() const { return t_[p_]; }
    const Token& previous() const { return t_[p_ - 1]; }
    bool check(Tok k) const { return t_[p_].type == k; }
    bool match(Tok k);
    const Token& expect(Tok k, const char* what);
    [[noreturn]] void errorAtCurrent(const std::string& expected);
    void synchronize();

    StmtPtr function();
    StmtPtr statement();
    StmtPtr letStatement(SourceLoc loc);
    StmtPtr forStatement(SourceLoc loc);
    std::vector<StmtPtr> block();  // after '{'

    ExprPtr expression();
    ExprPtr assignment();
    ExprPtr binary(ExprPtr (Parser::*next)(), std::initializer_list<Tok> ops, ExprKind kind);
    ExprPtr orExpr();
    ExprPtr andExpr();
    ExprPtr equality();
    ExprPtr comparison();
    ExprPtr term();
    ExprPtr factor();
    ExprPtr unary();
    ExprPtr postfix();
    ExprPtr primary();
};

}  // namespace mini
