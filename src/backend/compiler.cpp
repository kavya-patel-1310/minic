#include "backend/compiler.h"
#include "backend/builtins.h"

#include <algorithm>

namespace mini {

// RAII helpers keep the scope and loop stacks balanced even when a CompileError unwinds.
struct ScopeGuard {
    Compiler& c;
    int savedSlot;
    explicit ScopeGuard(Compiler& compiler) : c(compiler), savedSlot(compiler.nextSlot_) { c.scopes_.emplace_back(); }
    ~ScopeGuard() {
        c.scopes_.pop_back();
        c.nextSlot_ = savedSlot;  // the block's slots can be reused
    }
};

struct LoopGuard {
    Compiler& c;
    explicit LoopGuard(Compiler& compiler) : c(compiler) { c.loops_.emplace_back(); }
    ~LoopGuard() { c.loops_.pop_back(); }
    Compiler::Loop& loop() { return c.loops_.back(); }
};

// ------------------------------------------------------------------ helpers

void Compiler::emit(int32_t word, SourceLoc loc) {
    cur_->code.push_back(word);
    cur_->lines.push_back(loc.line);
}

void Compiler::adjustDepth(int delta) {
    depth_ += delta;
    cur_->maxStack = std::max(cur_->maxStack, depth_);
}

// Static stack-depth analysis: the VM uses maxStack to guarantee, once per call,
// that the frame fits in the preallocated value stack (no per-push bounds checks).
void Compiler::trackStack(Op op, int32_t operand, int32_t operand2) {
    switch (op) {
        case OP_CONST: case OP_NIL: case OP_LOAD: case OP_GLOAD: adjustDepth(+1); break;
        case OP_POP: case OP_JZ: case OP_JNZ: case OP_INDEX: case OP_ECHO: case OP_RET: adjustDepth(-1); break;
        case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD:
        case OP_EQ: case OP_NE: case OP_LT: case OP_LE: case OP_GT: case OP_GE: adjustDepth(-1); break;
        case OP_SETINDEX: adjustDepth(-2); break;
        case OP_CALL: adjustDepth(1 - program_.funcs[operand].arity); break;
        case OP_BUILTIN: adjustDepth(1 - operand2); break;
        case OP_ARRAY: adjustDepth(1 - operand); break;
        case OP_PRINT: adjustDepth(-operand); break;
        default: break;  // STORE, GSTORE, NEG, NOT, JMP, HALT leave the depth unchanged
    }
}

std::size_t Compiler::emitJump(Op op, SourceLoc loc) {
    emitOp(op, -1, loc);
    return cur_->code.size() - 1;  // position of the placeholder operand
}

void Compiler::patchJump(std::size_t at, std::size_t target) { cur_->code[at] = static_cast<int32_t>(target); }

int Compiler::constInt(int64_t v) {
    auto it = intConsts_.find(v);
    if (it != intConsts_.end()) return it->second;
    const int idx = static_cast<int>(program_.consts.size());
    program_.consts.push_back(Value::integer(v));
    intConsts_[v] = idx;
    return idx;
}

int Compiler::constStr(const std::string& s) {
    auto it = strConsts_.find(s);
    if (it != strConsts_.end()) return it->second;
    const int idx = static_cast<int>(program_.consts.size());
    program_.consts.push_back(Value::string(s));
    strConsts_[s] = idx;
    return idx;
}

int Compiler::declareGlobal(const std::string& name) {
    auto it = globalIndex_.find(name);
    if (it != globalIndex_.end()) return it->second;
    const int idx = static_cast<int>(program_.globals.size());
    program_.globals.push_back(name);
    globalIndex_[name] = idx;
    return idx;
}

int Compiler::declareLocal(const std::string& name, SourceLoc loc) {
    if (scopes_.back().count(name))
        throw CompileError("variable '" + name + "' is already declared in this scope", loc);
    const int slot = nextSlot_++;
    scopes_.back()[name] = slot;
    cur_->nlocals = std::max(cur_->nlocals, nextSlot_);
    return slot;
}

int Compiler::resolveLocal(const std::string& name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) return found->second;
    }
    return -1;
}

// ------------------------------------------------------------------- driver

int Compiler::compile(const std::vector<StmtPtr>& prog, const std::string& name) {
    // Snapshot for rollback if this chunk fails (matters for the REPL).
    const auto savedFns = fnIndex_;
    const auto savedGlobals = globalIndex_;
    const std::size_t savedFuncCount = program_.funcs.size();
    const std::size_t savedGlobalCount = program_.globals.size();
    const std::size_t errorsBefore = diag_->errorCount();

    const int entry = static_cast<int>(program_.funcs.size());
    Function script;
    script.name = name.empty() || name.front() != '<' ? "<" + name + ">" : name;
    program_.funcs.push_back(script);

    // Pass 1: declare functions and globals so they can be used before their definition.
    std::vector<std::pair<const Stmt*, int>> functions;
    for (const auto& s : prog) {
        if (s->kind == StmtKind::Fn) {
            try {
                if (findBuiltin(s->name) >= 0)
                    throw CompileError("'" + s->name + "' is a built-in function and cannot be redefined", s->loc);
                if (fnIndex_.count(s->name))
                    throw CompileError("function '" + s->name + "' is already defined", s->loc);
                const int idx = static_cast<int>(program_.funcs.size());
                fnIndex_[s->name] = idx;
                Function f;
                f.name = s->name;
                f.arity = static_cast<int>(s->params.size());
                program_.funcs.push_back(f);
                functions.emplace_back(s.get(), idx);
            } catch (const CompileError& e) {
                diag_->error(e);
            }
        } else if (s->kind == StmtKind::Let) {
            declareGlobal(s->name);
        }
    }
    // program_.funcs does not grow below this point, so cur_ stays valid.

    // Pass 2: function bodies.
    for (const auto& [fn, idx] : functions) {
        try {
            compileFunction(*fn, idx);
        } catch (const CompileError& e) {
            diag_->error(e);
        }
    }

    // Pass 3: the top-level script.
    cur_ = &program_.funcs[entry];
    scopes_.clear();
    loops_.clear();
    nextSlot_ = 0;
    depth_ = 0;
    SourceLoc last{1, 1};
    for (const auto& s : prog) {
        if (s->kind == StmtKind::Fn) continue;
        try {
            stmt(*s);
        } catch (const CompileError& e) {
            diag_->error(e);
        }
        last = s->loc;
    }
    emitOp(OP_HALT, last);

    if (diag_->errorCount() > errorsBefore) {
        fnIndex_ = savedFns;
        globalIndex_ = savedGlobals;
        program_.funcs.resize(savedFuncCount);
        program_.globals.resize(savedGlobalCount);
        return -1;
    }
    return entry;
}

void Compiler::compileFunction(const Stmt& fn, int index) {
    cur_ = &program_.funcs[index];
    scopes_.clear();
    loops_.clear();
    nextSlot_ = 0;
    depth_ = 0;
    ScopeGuard params(*this);
    for (const auto& p : fn.params) {
        if (scopes_.back().count(p))
            throw CompileError("duplicate parameter '" + p + "' in function '" + fn.name + "'", fn.loc);
        declareLocal(p, fn.loc);
    }

    stmtList(fn.stmts);

    // Falling off the end returns nil.
    const SourceLoc end = fn.stmts.empty() ? fn.loc : fn.stmts.back()->loc;
    emitOp(OP_NIL, end);
    emitOp(OP_RET, end);
}

// --------------------------------------------------------------- statements

void Compiler::stmtList(const std::vector<StmtPtr>& stmts) {
    for (const auto& s : stmts) {
        try {
            stmt(*s);
        } catch (const CompileError& e) {
            diag_->error(e);  // keep going to report more errors
        }
    }
}

// Bodies of if/while/for get their own scope, even without braces.
void Compiler::scopedStmt(const Stmt& s) {
    ScopeGuard scope(*this);
    stmt(s);
}

void Compiler::stmt(const Stmt& s) {
    const SourceLoc loc = s.loc;
    switch (s.kind) {
        case StmtKind::Let: {
            expr(*s.expr);  // compiled before declaring, so `let x = x + 1;` sees the outer x
            if (scopes_.empty()) {
                emitOp(OP_GSTORE, declareGlobal(s.name), loc);
            } else {
                emitOp(OP_STORE, declareLocal(s.name, loc), loc);
            }
            emitOp(OP_POP, loc);
            break;
        }
        case StmtKind::ExprStmt:
            expr(*s.expr);
            emitOp(s.echo ? OP_ECHO : OP_POP, loc);
            break;

        case StmtKind::Print:
            for (const auto& a : s.args) expr(*a);
            emitOp(OP_PRINT, static_cast<int32_t>(s.args.size()), loc);
            break;

        case StmtKind::If: {
            expr(*s.expr);
            const std::size_t toElse = emitJump(OP_JZ, loc);
            scopedStmt(*s.thenBranch);
            if (s.elseBranch) {
                const std::size_t toEnd = emitJump(OP_JMP, loc);
                patchJump(toElse, here());
                scopedStmt(*s.elseBranch);
                patchJump(toEnd, here());
            } else {
                patchJump(toElse, here());
            }
            break;
        }

        case StmtKind::While: {
            const std::size_t start = here();
            expr(*s.expr);
            const std::size_t exit = emitJump(OP_JZ, loc);
            LoopGuard guard(*this);
            scopedStmt(*s.body);
            emitOp(OP_JMP, static_cast<int32_t>(start), loc);
            patchJump(exit, here());
            for (std::size_t b : guard.loop().breaks) patchJump(b, here());
            for (std::size_t c : guard.loop().continues) patchJump(c, start);
            break;
        }

        case StmtKind::For: {
            ScopeGuard scope(*this);  // the loop variable lives here
            if (s.init) stmt(*s.init);
            const std::size_t start = here();
            std::size_t exit = 0;
            const bool hasCond = static_cast<bool>(s.expr);
            if (hasCond) {
                expr(*s.expr);
                exit = emitJump(OP_JZ, loc);
            }
            LoopGuard guard(*this);
            scopedStmt(*s.body);
            const std::size_t continueTarget = here();
            if (s.step) {
                expr(*s.step);
                emitOp(OP_POP, loc);
            }
            emitOp(OP_JMP, static_cast<int32_t>(start), loc);
            if (hasCond) patchJump(exit, here());
            for (std::size_t b : guard.loop().breaks) patchJump(b, here());
            for (std::size_t c : guard.loop().continues) patchJump(c, continueTarget);
            break;
        }

        case StmtKind::Break:
        case StmtKind::Continue: {
            const bool isBreak = s.kind == StmtKind::Break;
            if (loops_.empty())
                throw CompileError(std::string("'") + (isBreak ? "break" : "continue") + "' outside of a loop", loc);
            const std::size_t j = emitJump(OP_JMP, loc);
            (isBreak ? loops_.back().breaks : loops_.back().continues).push_back(j);
            break;
        }

        case StmtKind::Block: {
            ScopeGuard scope(*this);
            stmtList(s.stmts);
            break;
        }

        case StmtKind::Return:
            if (cur_ == nullptr || cur_->name.front() == '<')
                throw CompileError("'return' outside of a function", loc);
            if (s.expr) expr(*s.expr);
            else emitOp(OP_NIL, loc);
            emitOp(OP_RET, loc);
            depth_ = 0;  // statements always start and end at depth 0
            break;

        case StmtKind::Fn:
            throw CompileError("functions can only be declared at the top level", loc);
    }
}

// -------------------------------------------------------------- expressions

void Compiler::expr(const Expr& e) {
    const SourceLoc loc = e.loc;
    switch (e.kind) {
        case ExprKind::Int:
            emitOp(OP_CONST, constInt(e.intValue), loc);
            break;
        case ExprKind::Str:
            emitOp(OP_CONST, constStr(e.str), loc);
            break;
        case ExprKind::Nil:
            emitOp(OP_NIL, loc);
            break;

        case ExprKind::Var: {
            const int slot = resolveLocal(e.str);
            if (slot >= 0) emitOp(OP_LOAD, slot, loc);
            else if (globalIndex_.count(e.str)) emitOp(OP_GLOAD, globalIndex_[e.str], loc);
            else if (fnIndex_.count(e.str) || findBuiltin(e.str) >= 0)
                throw CompileError("'" + e.str + "' is a function; call it with " + e.str + "(...)", loc);
            else throw CompileError("undefined variable '" + e.str + "'", loc);
            break;
        }
        case ExprKind::Assign: {
            expr(*e.value);
            const int slot = resolveLocal(e.str);
            if (slot >= 0) emitOp(OP_STORE, slot, loc);
            else if (globalIndex_.count(e.str)) emitOp(OP_GSTORE, globalIndex_[e.str], loc);
            else throw CompileError("assignment to undeclared variable '" + e.str + "' (declare it with 'let')", loc);
            break;
        }

        case ExprKind::Unary:
            expr(*e.left);
            emitOp(e.op == Tok::Minus ? OP_NEG : OP_NOT, loc);
            break;

        case ExprKind::Binary: {
            expr(*e.left);
            expr(*e.right);
            Op op = OP_ADD;
            switch (e.op) {
                case Tok::Plus: op = OP_ADD; break;
                case Tok::Minus: op = OP_SUB; break;
                case Tok::Star: op = OP_MUL; break;
                case Tok::Slash: op = OP_DIV; break;
                case Tok::Percent: op = OP_MOD; break;
                case Tok::Eq: op = OP_EQ; break;
                case Tok::Ne: op = OP_NE; break;
                case Tok::Lt: op = OP_LT; break;
                case Tok::Le: op = OP_LE; break;
                case Tok::Gt: op = OP_GT; break;
                case Tok::Ge: op = OP_GE; break;
                default: throw CompileError("unknown binary operator", loc);
            }
            emitOp(op, loc);
            break;
        }

        case ExprKind::Logical: {
            // Short-circuit; the result is always 0 or 1.
            //   a && b:  a; JZ F; b; JZ F; 1; JMP E; F: 0; E:
            //   a || b:  a; JNZ T; b; JNZ T; 0; JMP E; T: 1; E:
            const bool isAnd = e.op == Tok::AndAnd;
            const Op test = isAnd ? OP_JZ : OP_JNZ;
            expr(*e.left);
            const std::size_t j1 = emitJump(test, loc);
            expr(*e.right);
            const std::size_t j2 = emitJump(test, loc);
            emitOp(OP_CONST, constInt(isAnd ? 1 : 0), loc);
            const std::size_t end = emitJump(OP_JMP, loc);
            adjustDepth(-1);  // the other path joins here without the first constant
            patchJump(j1, here());
            patchJump(j2, here());
            emitOp(OP_CONST, constInt(isAnd ? 0 : 1), loc);
            patchJump(end, here());
            break;
        }

        case ExprKind::Call:
            call(e);
            break;

        case ExprKind::Array:
            for (const auto& item : e.items) expr(*item);
            emitOp(OP_ARRAY, static_cast<int32_t>(e.items.size()), loc);
            break;

        case ExprKind::Index:
            expr(*e.left);
            expr(*e.right);
            emitOp(OP_INDEX, loc);
            break;

        case ExprKind::IndexAssign:
            expr(*e.left);
            expr(*e.right);
            expr(*e.value);
            emitOp(OP_SETINDEX, loc);
            break;
    }
}

void Compiler::call(const Expr& e) {
    const int argc = static_cast<int>(e.items.size());
    auto it = fnIndex_.find(e.str);
    if (it != fnIndex_.end()) {
        const Function& f = program_.funcs[it->second];
        if (argc != f.arity)
            throw CompileError("function '" + e.str + "' expects " + std::to_string(f.arity) + " argument" +
                                   (f.arity == 1 ? "" : "s") + " but got " + std::to_string(argc),
                               e.loc);
        for (const auto& a : e.items) expr(*a);
        emitOp(OP_CALL, it->second, e.loc);
        return;
    }

    const int b = findBuiltin(e.str);
    if (b < 0) throw CompileError("undefined function '" + e.str + "'", e.loc);
    const BuiltinInfo& info = builtinInfo(b);
    if (argc < info.minArgs || argc > info.maxArgs)
        throw CompileError("wrong number of arguments to built-in: " + std::string(info.signature), e.loc);
    for (const auto& a : e.items) expr(*a);
    trackStack(OP_BUILTIN, b, argc);
    emit(OP_BUILTIN, e.loc);
    emit(b, e.loc);
    emit(argc, e.loc);
}

}  // namespace mini
