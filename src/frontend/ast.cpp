#include "frontend/ast.h"

namespace mini {

ExprPtr makeExpr(ExprKind kind, SourceLoc loc) {
    auto e = std::make_unique<Expr>();
    e->kind = kind;
    e->loc = loc;
    return e;
}

StmtPtr makeStmt(StmtKind kind, SourceLoc loc) {
    auto s = std::make_unique<Stmt>();
    s->kind = kind;
    s->loc = loc;
    return s;
}

ExprPtr cloneExpr(const Expr& e) {
    auto c = makeExpr(e.kind, e.loc);
    c->intValue = e.intValue;
    c->str = e.str;
    c->op = e.op;
    if (e.left) c->left = cloneExpr(*e.left);
    if (e.right) c->right = cloneExpr(*e.right);
    if (e.value) c->value = cloneExpr(*e.value);
    for (const auto& item : e.items) c->items.push_back(cloneExpr(*item));
    return c;
}

// ------------------------------------------------------------- printing

namespace {

std::string quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            default: out += c;
        }
    }
    return out + "\"";
}

std::string list(const std::vector<ExprPtr>& items) {
    std::string out;
    for (const auto& item : items) out += " " + exprToString(*item);
    return out;
}

void printStmt(const Stmt& s, std::ostream& out, int indent);

void printChild(const Stmt* s, std::ostream& out, int indent) {
    if (!s) {
        out << std::string(indent * 2, ' ') << "(empty)\n";
        return;
    }
    printStmt(*s, out, indent);
}

void printStmt(const Stmt& s, std::ostream& out, int indent) {
    const std::string pad(indent * 2, ' ');
    switch (s.kind) {
        case StmtKind::Let:
            out << pad << "(let " << s.name << " " << exprToString(*s.expr) << ")\n";
            break;
        case StmtKind::ExprStmt:
            out << pad << "(expr " << exprToString(*s.expr) << ")\n";
            break;
        case StmtKind::Print:
            out << pad << "(print" << list(s.args) << ")\n";
            break;
        case StmtKind::If:
            out << pad << "(if " << exprToString(*s.expr) << "\n";
            printChild(s.thenBranch.get(), out, indent + 1);
            if (s.elseBranch) printChild(s.elseBranch.get(), out, indent + 1);
            out << pad << ")\n";
            break;
        case StmtKind::While:
            out << pad << "(while " << exprToString(*s.expr) << "\n";
            printChild(s.body.get(), out, indent + 1);
            out << pad << ")\n";
            break;
        case StmtKind::For:
            out << pad << "(for\n";
            printChild(s.init.get(), out, indent + 1);
            out << pad << "  " << (s.expr ? exprToString(*s.expr) : "(empty)") << "\n";
            out << pad << "  " << (s.step ? exprToString(*s.step) : "(empty)") << "\n";
            printChild(s.body.get(), out, indent + 1);
            out << pad << ")\n";
            break;
        case StmtKind::Break:
            out << pad << "(break)\n";
            break;
        case StmtKind::Continue:
            out << pad << "(continue)\n";
            break;
        case StmtKind::Return:
            out << pad << "(return" << (s.expr ? " " + exprToString(*s.expr) : "") << ")\n";
            break;
        case StmtKind::Block:
            out << pad << "(block\n";
            for (const auto& c : s.stmts) printStmt(*c, out, indent + 1);
            out << pad << ")\n";
            break;
        case StmtKind::Fn: {
            out << pad << "(fn " << s.name << " (";
            for (std::size_t i = 0; i < s.params.size(); ++i) out << (i ? " " : "") << s.params[i];
            out << ")\n";
            for (const auto& c : s.stmts) printStmt(*c, out, indent + 1);
            out << pad << ")\n";
            break;
        }
    }
}

}  // namespace

std::string exprToString(const Expr& e) {
    switch (e.kind) {
        case ExprKind::Int: return std::to_string(e.intValue);
        case ExprKind::Str: return quote(e.str);
        case ExprKind::Nil: return "nil";
        case ExprKind::Var: return e.str;
        case ExprKind::Assign: return "(= " + e.str + " " + exprToString(*e.value) + ")";
        case ExprKind::Unary: return std::string("(") + tokName(e.op) + " " + exprToString(*e.left) + ")";
        case ExprKind::Binary:
        case ExprKind::Logical:
            return std::string("(") + tokName(e.op) + " " + exprToString(*e.left) + " " + exprToString(*e.right) + ")";
        case ExprKind::Call: return "(call " + e.str + list(e.items) + ")";
        case ExprKind::Array: return "(array" + list(e.items) + ")";
        case ExprKind::Index: return "(index " + exprToString(*e.left) + " " + exprToString(*e.right) + ")";
        case ExprKind::IndexAssign:
            return "(= (index " + exprToString(*e.left) + " " + exprToString(*e.right) + ") " +
                   exprToString(*e.value) + ")";
    }
    return "?";
}

void printAst(const std::vector<StmtPtr>& program, std::ostream& out) {
    for (const auto& s : program) printStmt(*s, out, 0);
}

}  // namespace mini
