#include "frontend/parser.h"

namespace mini {

Parser::Parser(std::vector<Token> tokens, Diagnostics& diag) : t_(std::move(tokens)), diag_(diag) {}

bool Parser::match(Tok k) {
    if (!check(k)) return false;
    ++p_;
    return true;
}

void Parser::errorAtCurrent(const std::string& expected) {
    const Token& tok = peek();
    std::string found = tok.type == Tok::End ? "end of file" : "'" + tok.text + "'";
    throw CompileError("expected " + expected + ", found " + found, tok.loc);
}

const Token& Parser::expect(Tok k, const char* what) {
    if (!check(k)) {
        // Point at the end of the previous token for a missing ';' or ')': that's where it belongs.
        if ((k == Tok::Semi || k == Tok::RParen) && p_ > 0 && previous().loc.line != peek().loc.line) {
            const Token& prev = previous();
            int width = prev.type == Tok::Str ? static_cast<int>(prev.text.size()) + 2 : static_cast<int>(prev.text.size());
            std::string found = peek().type == Tok::End ? "end of file" : "'" + peek().text + "'";
            throw CompileError(std::string("expected ") + what + ", found " + found,
                               {prev.loc.line, prev.loc.col + width});
        }
        errorAtCurrent(what);
    }
    return t_[p_++];
}

// Skip tokens until a likely statement boundary. A '{' reached while skipping
// belongs to the construct whose header failed (fn, if, while, ...), so its whole
// body is skipped too; otherwise the body would produce a cascade of bogus errors.
void Parser::synchronize() {
    while (!check(Tok::End)) {
        if (p_ > 0 && previous().type == Tok::Semi) return;
        if (check(Tok::LBrace)) {
            int depth = 0;
            do {
                if (check(Tok::LBrace)) ++depth;
                else if (check(Tok::RBrace)) --depth;
                ++p_;
            } while (depth > 0 && !check(Tok::End));
            return;
        }
        switch (peek().type) {
            case Tok::Let: case Tok::Fn: case Tok::If: case Tok::While: case Tok::For:
            case Tok::Return: case Tok::Print: case Tok::Break: case Tok::Continue:
            case Tok::RBrace:
                return;
            default:
                ++p_;
        }
    }
}

// ---------------------------------------------------------------- statements

std::vector<StmtPtr> Parser::parseProgram() {
    std::vector<StmtPtr> program;
    while (!check(Tok::End)) {
        const std::size_t start = p_;
        try {
            if (check(Tok::RBrace)) {
                if (recovering_) {  // most likely the end of a block whose header failed to parse
                    ++p_;
                    continue;
                }
                throw CompileError("unexpected '}'", peek().loc);
            }
            program.push_back(check(Tok::Fn) ? function() : statement());
            recovering_ = false;
        } catch (const CompileError& e) {
            diag_.error(e);
            recovering_ = true;
            synchronize();
            if (p_ == start) ++p_;  // always make progress
        }
    }
    return program;
}

StmtPtr Parser::function() {
    const SourceLoc loc = expect(Tok::Fn, "'fn'").loc;
    auto s = makeStmt(StmtKind::Fn, loc);
    s->name = expect(Tok::Ident, "function name").text;
    expect(Tok::LParen, "'(' after function name");
    if (!check(Tok::RParen)) {
        do {
            s->params.push_back(expect(Tok::Ident, "parameter name").text);
        } while (match(Tok::Comma));
    }
    expect(Tok::RParen, "')' after parameters");
    expect(Tok::LBrace, "'{' before function body");
    s->stmts = block();
    return s;
}

std::vector<StmtPtr> Parser::block() {
    std::vector<StmtPtr> body;
    while (!check(Tok::RBrace) && !check(Tok::End)) {
        const std::size_t start = p_;
        try {
            if (check(Tok::Fn)) throw CompileError("functions can only be declared at the top level", peek().loc);
            body.push_back(statement());
            recovering_ = false;
        } catch (const CompileError& e) {
            diag_.error(e);
            recovering_ = true;
            synchronize();
            if (p_ == start) ++p_;
        }
    }
    expect(Tok::RBrace, "'}'");
    return body;
}

StmtPtr Parser::letStatement(SourceLoc loc) {
    auto s = makeStmt(StmtKind::Let, loc);
    s->name = expect(Tok::Ident, "variable name after 'let'").text;
    expect(Tok::Assign, "'=' (variables must be initialised)");
    s->expr = expression();
    expect(Tok::Semi, "';' after variable declaration");
    return s;
}

StmtPtr Parser::forStatement(SourceLoc loc) {
    auto s = makeStmt(StmtKind::For, loc);
    expect(Tok::LParen, "'(' after 'for'");

    if (match(Tok::Semi)) {
        // no initializer
    } else if (check(Tok::Let)) {
        const SourceLoc letLoc = peek().loc;
        ++p_;
        s->init = letStatement(letLoc);
    } else {
        auto init = makeStmt(StmtKind::ExprStmt, peek().loc);
        init->expr = expression();
        expect(Tok::Semi, "';' after loop initializer");
        s->init = std::move(init);
    }

    if (!check(Tok::Semi)) s->expr = expression();
    expect(Tok::Semi, "';' after loop condition");
    if (!check(Tok::RParen)) s->step = expression();
    expect(Tok::RParen, "')' after for clauses");

    s->body = statement();
    return s;
}

StmtPtr Parser::statement() {
    const SourceLoc loc = peek().loc;

    if (match(Tok::Let)) return letStatement(loc);
    if (match(Tok::For)) return forStatement(loc);

    if (match(Tok::Print)) {
        auto s = makeStmt(StmtKind::Print, loc);
        do {
            s->args.push_back(expression());
        } while (match(Tok::Comma));
        expect(Tok::Semi, "';' after print statement");
        return s;
    }
    if (match(Tok::If)) {
        auto s = makeStmt(StmtKind::If, loc);
        expect(Tok::LParen, "'(' after 'if'");
        s->expr = expression();
        expect(Tok::RParen, "')' after condition");
        s->thenBranch = statement();
        if (match(Tok::Else)) s->elseBranch = statement();
        return s;
    }
    if (match(Tok::While)) {
        auto s = makeStmt(StmtKind::While, loc);
        expect(Tok::LParen, "'(' after 'while'");
        s->expr = expression();
        expect(Tok::RParen, "')' after condition");
        s->body = statement();
        return s;
    }
    if (match(Tok::Break) || match(Tok::Continue)) {
        auto s = makeStmt(previous().type == Tok::Break ? StmtKind::Break : StmtKind::Continue, loc);
        expect(Tok::Semi, s->kind == StmtKind::Break ? "';' after 'break'" : "';' after 'continue'");
        return s;
    }
    if (match(Tok::Return)) {
        auto s = makeStmt(StmtKind::Return, loc);
        if (!check(Tok::Semi)) s->expr = expression();
        expect(Tok::Semi, "';' after return value");
        return s;
    }
    if (match(Tok::LBrace)) {
        auto s = makeStmt(StmtKind::Block, loc);
        s->stmts = block();
        return s;
    }

    auto s = makeStmt(StmtKind::ExprStmt, loc);
    s->expr = expression();
    expect(Tok::Semi, "';' after expression");
    return s;
}

// --------------------------------------------------------------- expressions

ExprPtr Parser::expression() { return assignment(); }

namespace {
bool isAssignOp(Tok t) {
    return t == Tok::Assign || t == Tok::PlusAssign || t == Tok::MinusAssign || t == Tok::StarAssign ||
           t == Tok::SlashAssign || t == Tok::PercentAssign;
}
Tok baseOp(Tok t) {
    switch (t) {
        case Tok::PlusAssign: return Tok::Plus;
        case Tok::MinusAssign: return Tok::Minus;
        case Tok::StarAssign: return Tok::Star;
        case Tok::SlashAssign: return Tok::Slash;
        case Tok::PercentAssign: return Tok::Percent;
        default: return Tok::End;
    }
}
}  // namespace

ExprPtr Parser::assignment() {
    ExprPtr target = orExpr();
    if (!isAssignOp(peek().type)) return target;

    const Token opTok = t_[p_++];
    ExprPtr value = assignment();  // right associative: a = b = 3

    // Desugar compound assignment: a += b  =>  a = a + b
    if (opTok.type != Tok::Assign) {
        auto bin = makeExpr(ExprKind::Binary, opTok.loc);
        bin->op = baseOp(opTok.type);
        bin->left = cloneExpr(*target);
        bin->right = std::move(value);
        value = std::move(bin);
    }

    if (target->kind == ExprKind::Var) {
        auto e = makeExpr(ExprKind::Assign, target->loc);
        e->str = target->str;
        e->value = std::move(value);
        return e;
    }
    if (target->kind == ExprKind::Index) {
        auto e = makeExpr(ExprKind::IndexAssign, target->loc);
        e->left = std::move(target->left);
        e->right = std::move(target->right);
        e->value = std::move(value);
        return e;
    }
    throw CompileError("invalid assignment target", target->loc);
}

ExprPtr Parser::binary(ExprPtr (Parser::*next)(), std::initializer_list<Tok> ops, ExprKind kind) {
    ExprPtr left = (this->*next)();
    for (;;) {
        bool matched = false;
        for (Tok op : ops) {
            if (!check(op)) continue;
            auto e = makeExpr(kind, peek().loc);
            e->op = op;
            ++p_;
            e->left = std::move(left);
            e->right = (this->*next)();
            left = std::move(e);
            matched = true;
            break;
        }
        if (!matched) return left;
    }
}

ExprPtr Parser::orExpr()     { return binary(&Parser::andExpr, {Tok::OrOr}, ExprKind::Logical); }
ExprPtr Parser::andExpr()    { return binary(&Parser::equality, {Tok::AndAnd}, ExprKind::Logical); }
ExprPtr Parser::equality()   { return binary(&Parser::comparison, {Tok::Eq, Tok::Ne}, ExprKind::Binary); }
ExprPtr Parser::comparison() { return binary(&Parser::term, {Tok::Lt, Tok::Le, Tok::Gt, Tok::Ge}, ExprKind::Binary); }
ExprPtr Parser::term()       { return binary(&Parser::factor, {Tok::Plus, Tok::Minus}, ExprKind::Binary); }
ExprPtr Parser::factor()     { return binary(&Parser::unary, {Tok::Star, Tok::Slash, Tok::Percent}, ExprKind::Binary); }

ExprPtr Parser::unary() {
    if (check(Tok::Minus) || check(Tok::Bang)) {
        auto e = makeExpr(ExprKind::Unary, peek().loc);
        e->op = t_[p_++].type;
        e->left = unary();
        return e;
    }
    return postfix();
}

ExprPtr Parser::postfix() {
    ExprPtr e = primary();
    while (check(Tok::LBracket)) {
        auto idx = makeExpr(ExprKind::Index, peek().loc);
        ++p_;
        idx->left = std::move(e);
        idx->right = expression();
        expect(Tok::RBracket, "']' after index");
        e = std::move(idx);
    }
    return e;
}

ExprPtr Parser::primary() {
    const Token& tok = peek();
    const SourceLoc loc = tok.loc;

    if (match(Tok::Int)) {
        auto e = makeExpr(ExprKind::Int, loc);
        e->intValue = previous().value;
        return e;
    }
    if (match(Tok::True) || match(Tok::False)) {
        auto e = makeExpr(ExprKind::Int, loc);
        e->intValue = previous().type == Tok::True ? 1 : 0;
        return e;
    }
    if (match(Tok::Nil)) return makeExpr(ExprKind::Nil, loc);
    if (match(Tok::Str)) {
        auto e = makeExpr(ExprKind::Str, loc);
        e->str = previous().text;
        return e;
    }
    if (match(Tok::Ident)) {
        const std::string name = previous().text;
        if (match(Tok::LParen)) {
            auto e = makeExpr(ExprKind::Call, loc);
            e->str = name;
            if (!check(Tok::RParen)) {
                do {
                    e->items.push_back(expression());
                } while (match(Tok::Comma));
            }
            expect(Tok::RParen, "')' after arguments");
            return e;
        }
        auto e = makeExpr(ExprKind::Var, loc);
        e->str = name;
        return e;
    }
    if (match(Tok::LBracket)) {
        auto e = makeExpr(ExprKind::Array, loc);
        while (!check(Tok::RBracket)) {
            e->items.push_back(expression());
            if (!match(Tok::Comma)) break;  // trailing comma allowed
        }
        expect(Tok::RBracket, "']' after array elements");
        return e;
    }
    if (match(Tok::LParen)) {
        ExprPtr inner = expression();
        expect(Tok::RParen, "')'");
        return inner;
    }
    errorAtCurrent("an expression");
}

}  // namespace mini
