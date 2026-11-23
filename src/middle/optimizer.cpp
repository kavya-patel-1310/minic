#include "middle/optimizer.h"

#include <algorithm>
#include <limits>

namespace mini {

namespace {

bool isLiteral(const Expr& e) {
    return e.kind == ExprKind::Int || e.kind == ExprKind::Str || e.kind == ExprKind::Nil;
}

bool literalTruthy(const Expr& e) {
    switch (e.kind) {
        case ExprKind::Int: return e.intValue != 0;
        case ExprKind::Str: return !e.str.empty();
        default: return false;  // nil
    }
}

std::string literalText(const Expr& e) {
    switch (e.kind) {
        case ExprKind::Int: return std::to_string(e.intValue);
        case ExprKind::Str: return e.str;
        default: return "nil";
    }
}

int64_t wrap(uint64_t v) { return static_cast<int64_t>(v); }

class Optimizer {
public:
    OptimizerStats stats;

    void program(std::vector<StmtPtr>& stmts) {
        for (auto& s : stmts) stmt(s);
        removeNulls(stmts);
    }

private:
    static void removeNulls(std::vector<StmtPtr>& stmts) {
        stmts.erase(std::remove(stmts.begin(), stmts.end(), nullptr), stmts.end());
    }

    static bool isTerminator(const Stmt& s) {
        return s.kind == StmtKind::Return || s.kind == StmtKind::Break || s.kind == StmtKind::Continue;
    }

    // Keeps a replacement statement in its own scope so a `let` inside it doesn't leak.
    static StmtPtr scoped(StmtPtr s) {
        if (!s || s->kind == StmtKind::Block) return s;
        auto block = makeStmt(StmtKind::Block, s->loc);
        block->stmts.push_back(std::move(s));
        return block;
    }

    static StmtPtr emptyBlock(SourceLoc loc) { return makeStmt(StmtKind::Block, loc); }

    void block(std::vector<StmtPtr>& stmts) {
        for (auto& s : stmts) stmt(s);
        removeNulls(stmts);
        for (std::size_t i = 0; i < stmts.size(); ++i) {
            if (isTerminator(*stmts[i]) && i + 1 < stmts.size()) {
                stats.deadStatements += static_cast<int>(stmts.size() - i - 1);
                stmts.resize(i + 1);
                break;
            }
        }
    }

    void stmt(StmtPtr& s) {
        switch (s->kind) {
            case StmtKind::Let:
                expr(s->expr);
                break;
            case StmtKind::ExprStmt:
                expr(s->expr);
                if (!s->echo && isLiteral(*s->expr)) {  // e.g. `1 + 2;` has no effect
                    ++stats.deadStatements;
                    s = nullptr;
                }
                break;
            case StmtKind::Print:
                for (auto& a : s->args) expr(a);
                break;
            case StmtKind::Return:
                if (s->expr) expr(s->expr);
                break;
            case StmtKind::Break:
            case StmtKind::Continue:
                break;
            case StmtKind::Block:
            case StmtKind::Fn:
                block(s->stmts);
                break;

            case StmtKind::If: {
                expr(s->expr);
                stmt(s->thenBranch);
                if (!s->thenBranch) s->thenBranch = emptyBlock(s->loc);
                if (s->elseBranch) stmt(s->elseBranch);
                if (isLiteral(*s->expr)) {
                    ++stats.branchesRemoved;
                    s = scoped(literalTruthy(*s->expr) ? std::move(s->thenBranch) : std::move(s->elseBranch));
                }
                break;
            }
            case StmtKind::While:
                expr(s->expr);
                stmt(s->body);
                if (!s->body) s->body = emptyBlock(s->loc);
                if (isLiteral(*s->expr) && !literalTruthy(*s->expr)) {
                    ++stats.branchesRemoved;
                    s = nullptr;
                }
                break;

            case StmtKind::For:
                if (s->init) stmt(s->init);
                if (s->expr) expr(s->expr);
                if (s->step) expr(s->step);
                stmt(s->body);
                if (!s->body) s->body = emptyBlock(s->loc);
                if (s->expr && isLiteral(*s->expr) && !literalTruthy(*s->expr)) {
                    ++stats.branchesRemoved;
                    s = scoped(std::move(s->init));  // the initializer still runs once
                }
                break;
        }
    }

    void replaceInt(ExprPtr& e, int64_t v) {
        auto lit = makeExpr(ExprKind::Int, e->loc);
        lit->intValue = v;
        e = std::move(lit);
        ++stats.constantsFolded;
    }

    void replaceStr(ExprPtr& e, std::string v) {
        auto lit = makeExpr(ExprKind::Str, e->loc);
        lit->str = std::move(v);
        e = std::move(lit);
        ++stats.constantsFolded;
    }

    void expr(ExprPtr& e) {
        switch (e->kind) {
            case ExprKind::Int:
            case ExprKind::Str:
            case ExprKind::Nil:
            case ExprKind::Var:
                return;
            case ExprKind::Assign:
                expr(e->value);
                return;
            case ExprKind::IndexAssign:
                expr(e->left);
                expr(e->right);
                expr(e->value);
                return;
            case ExprKind::Index:
                expr(e->left);
                expr(e->right);
                return;
            case ExprKind::Call:
            case ExprKind::Array:
                for (auto& item : e->items) expr(item);
                return;
            case ExprKind::Unary:
                expr(e->left);
                foldUnary(e);
                return;
            case ExprKind::Binary:
                expr(e->left);
                expr(e->right);
                foldBinary(e);
                return;
            case ExprKind::Logical:
                expr(e->left);
                expr(e->right);
                foldLogical(e);
                return;
        }
    }

    void foldUnary(ExprPtr& e) {
        const Expr& operand = *e->left;
        if (e->op == Tok::Bang && isLiteral(operand)) return replaceInt(e, !literalTruthy(operand));
        if (e->op == Tok::Minus && operand.kind == ExprKind::Int)
            return replaceInt(e, wrap(0 - static_cast<uint64_t>(operand.intValue)));
    }

    void foldLogical(ExprPtr& e) {
        const Expr& l = *e->left;
        const Expr& r = *e->right;
        if (!isLiteral(l)) return;
        const bool isAnd = e->op == Tok::AndAnd;
        const bool lt = literalTruthy(l);
        if (isAnd && !lt) return replaceInt(e, 0);  // false && x  ->  0  (x never runs)
        if (!isAnd && lt) return replaceInt(e, 1);  // true  || x  ->  1
        if (isLiteral(r)) return replaceInt(e, literalTruthy(r));
    }

    void foldBinary(ExprPtr& e) {
        const Expr& l = *e->left;
        const Expr& r = *e->right;
        if (!isLiteral(l) || !isLiteral(r)) return;

        if (l.kind == ExprKind::Int && r.kind == ExprKind::Int) {
            const int64_t a = l.intValue, b = r.intValue;
            const uint64_t ua = static_cast<uint64_t>(a), ub = static_cast<uint64_t>(b);
            switch (e->op) {
                case Tok::Plus: return replaceInt(e, wrap(ua + ub));
                case Tok::Minus: return replaceInt(e, wrap(ua - ub));
                case Tok::Star: return replaceInt(e, wrap(ua * ub));
                case Tok::Slash:
                case Tok::Percent: {
                    if (b == 0) return;  // keep: must fail at run time
                    const bool overflow = a == std::numeric_limits<int64_t>::min() && b == -1;
                    if (e->op == Tok::Slash) return replaceInt(e, overflow ? a : a / b);
                    return replaceInt(e, overflow ? 0 : a % b);
                }
                case Tok::Lt: return replaceInt(e, a < b);
                case Tok::Le: return replaceInt(e, a <= b);
                case Tok::Gt: return replaceInt(e, a > b);
                case Tok::Ge: return replaceInt(e, a >= b);
                case Tok::Eq: return replaceInt(e, a == b);
                case Tok::Ne: return replaceInt(e, a != b);
                default: return;
            }
        }

        const bool ls = l.kind == ExprKind::Str, rs = r.kind == ExprKind::Str;
        if (e->op == Tok::Plus && (ls || rs)) return replaceStr(e, literalText(l) + literalText(r));
        if (ls && rs) {
            switch (e->op) {
                case Tok::Eq: return replaceInt(e, l.str == r.str);
                case Tok::Ne: return replaceInt(e, l.str != r.str);
                case Tok::Lt: return replaceInt(e, l.str < r.str);
                case Tok::Le: return replaceInt(e, l.str <= r.str);
                case Tok::Gt: return replaceInt(e, l.str > r.str);
                case Tok::Ge: return replaceInt(e, l.str >= r.str);
                default: return;
            }
        }
        // Equality between literals of different kinds is always false.
        if ((e->op == Tok::Eq || e->op == Tok::Ne) && l.kind != r.kind)
            return replaceInt(e, e->op == Tok::Ne);
    }
};

}  // namespace

OptimizerStats optimize(std::vector<StmtPtr>& program) {
    Optimizer opt;
    opt.program(program);
    return opt.stats;
}

}  // namespace mini
