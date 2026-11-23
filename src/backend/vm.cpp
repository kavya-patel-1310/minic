#include "backend/vm.h"
#include "backend/builtins.h"

#include <charconv>
#include <limits>
#include <string>

namespace mini {

namespace {

const char* opSymbol(int32_t op) {
    switch (op) {
        case OP_ADD: return "+";
        case OP_SUB: return "-";
        case OP_MUL: return "*";
        case OP_DIV: return "/";
        case OP_MOD: return "%";
        case OP_LT: return "<";
        case OP_LE: return "<=";
        case OP_GT: return ">";
        case OP_GE: return ">=";
        default: return "?";
    }
}

// Copy without touching reference counts when neither side holds a heap object
// (the common case: integers and nil).
inline void assign(Value& dst, const Value& src) {
    if (!src.obj && !dst.obj) {
        dst.type = src.type;
        dst.i = src.i;
    } else {
        dst = src;
    }
}

inline void setInt(Value& dst, int64_t v) {
    if (dst.obj) {
        dst = Value::integer(v);
    } else {
        dst.type = ValueType::Int;
        dst.i = v;
    }
}

std::string operandTypes(const Value& a, const Value& b) {
    return std::string("'") + typeName(a.type) + "' and '" + typeName(b.type) + "'";
}

}  // namespace

VM::VM(const Program& program, std::ostream& out, std::istream& in)
    : P(program), out_(out), in_(in), start_(std::chrono::steady_clock::now()) {
    stack_.resize(4096);
}

// Builds the error with a stack trace (innermost frame first). Every frame's saved
// ip points just past the last word it consumed, so ip - 1 is inside the failing
// (or calling) instruction, and every word of an instruction shares its line.
void VM::fail(const std::string& message) {
    auto lineOf = [](const Frame& f) { return f.fn->lines[static_cast<std::size_t>(f.ip - 1 - f.fn->code.data())]; };
    RuntimeError err(message, frames_.empty() ? 0 : lineOf(frames_.back()));
    for (std::size_t k = frames_.size(); k-- > 0;) {
        const Frame& f = frames_[k];
        const std::string name = f.fn->name.front() == '<' ? f.fn->name : f.fn->name + "()";
        err.trace.push_back(name + " line " + std::to_string(lineOf(f)));
        if (err.trace.size() == 12 && k > 2) {  // keep deep recursion readable
            err.trace.push_back("... " + std::to_string(k - 2) + " more frames ...");
            k = 2;
        }
    }
    frames_.clear();
    throw err;
}

void VM::ensureStack(std::size_t needed) {
    if (needed <= stack_.size()) return;
    std::size_t size = stack_.size();
    while (size < needed) size *= 2;
    stack_.resize(size);
}

void VM::run(int entry) {
    globals_.resize(P.globals.size());
    assigned_.resize(P.globals.size(), 0);
    frames_.clear();
    const Function* script = &P.funcs[entry];
    ensureStack(static_cast<std::size_t>(script->nlocals + script->maxStack) + 16);
    for (int k = 0; k < script->nlocals; ++k) stack_[static_cast<std::size_t>(k)] = Value::nil();
    frames_.push_back({script, script->code.data(), 0});
    execute();
    out_.flush();
}

Value VM::index(const Value& object, const Value& idx) {
    if (!idx.isInt()) fail(std::string("index must be an int, not '") + typeName(idx.type) + "'");
    int64_t size = 0;
    if (object.isArray()) size = static_cast<int64_t>(object.items().size());
    else if (object.isStr()) size = static_cast<int64_t>(object.text().size());
    else fail(std::string("cannot index a value of type '") + typeName(object.type) + "'");

    int64_t k = idx.i < 0 ? idx.i + size : idx.i;  // negative indices count from the end
    if (k < 0 || k >= size)
        fail("index " + std::to_string(idx.i) + " out of range for length " + std::to_string(size));
    if (object.isArray()) return (object.items())[static_cast<std::size_t>(k)];
    return Value::string(std::string(1, (object.text())[static_cast<std::size_t>(k)]));
}

void VM::setIndex(const Value& object, const Value& idx, Value v) {
    if (object.isStr()) fail("strings are immutable");
    if (!object.isArray()) fail(std::string("cannot index a value of type '") + typeName(object.type) + "'");
    if (!idx.isInt()) fail(std::string("index must be an int, not '") + typeName(idx.type) + "'");
    const int64_t size = static_cast<int64_t>(object.items().size());
    const int64_t k = idx.i < 0 ? idx.i + size : idx.i;
    if (k < 0 || k >= size)
        fail("index " + std::to_string(idx.i) + " out of range for length " + std::to_string(size));
    (object.items())[static_cast<std::size_t>(k)] = std::move(v);
}

Value VM::builtin(int id, Value* a, int argc) {
    switch (static_cast<Builtin>(id)) {
        case Builtin::Len:
            if (a[0].isStr()) return Value::integer(static_cast<int64_t>(a[0].text().size()));
            if (a[0].isArray()) return Value::integer(static_cast<int64_t>(a[0].items().size()));
            fail(std::string("len() expects a string or array, got '") + typeName(a[0].type) + "'");

        case Builtin::Push:
            if (!a[0].isArray()) fail("push() expects an array as its first argument");
            a[0].items().push_back(a[1]);
            return Value::nil();

        case Builtin::Pop: {
            if (!a[0].isArray()) fail("pop() expects an array");
            if (a[0].items().empty()) fail("pop() from an empty array");
            Value v = std::move(a[0].items().back());
            a[0].items().pop_back();
            return v;
        }

        case Builtin::Str:
            return a[0].isStr() ? a[0] : Value::string(a[0].toString());

        case Builtin::Int: {
            if (a[0].isInt()) return a[0];
            if (!a[0].isStr()) fail(std::string("int() cannot convert '") + typeName(a[0].type) + "'");
            const std::string& s = a[0].text();
            std::size_t b = s.find_first_not_of(" \t\r\n"), e = s.find_last_not_of(" \t\r\n");
            int64_t v = 0;
            if (b != std::string::npos) {
                const char* first = s.data() + b;
                const char* last = s.data() + e + 1;
                if (*first == '+') ++first;
                auto [ptr, ec] = std::from_chars(first, last, v);
                if (ec == std::errc() && ptr == last) return Value::integer(v);
            }
            fail("int() cannot parse \"" + s + "\" as an integer");
        }

        case Builtin::Input: {
            if (argc == 1) {
                out_ << a[0].toString();
                out_.flush();
            }
            std::string lineText;
            if (!std::getline(in_, lineText)) return Value::nil();
            if (!lineText.empty() && lineText.back() == '\r') lineText.pop_back();
            return Value::string(lineText);
        }

        case Builtin::Clock: {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_);
            return Value::integer(ms.count());
        }

        case Builtin::Assert:
            if (!a[0].truthy()) fail(argc == 2 ? "assertion failed: " + a[1].toString() : "assertion failed");
            return Value::nil();

        case Builtin::Type:
            return Value::string(typeName(a[0].type));

        case Builtin::Abs:
            if (!a[0].isInt()) fail("abs() expects an int");
            return Value::integer(a[0].i < 0 ? static_cast<int64_t>(0 - static_cast<uint64_t>(a[0].i)) : a[0].i);

        case Builtin::Array: {
            if (!a[0].isInt() || a[0].i < 0) fail("array() expects a non-negative size");
            if (a[0].i > 100'000'000) fail("array() size too large");
            return Value::array(ArrayData(static_cast<std::size_t>(a[0].i), argc == 2 ? a[1] : Value::integer(0)));
        }

        case Builtin::Count:
            break;
    }
    fail("invalid built-in");
}

#if defined(__GNUC__) || defined(__clang__)
#define MINI_COMPUTED_GOTO 1
// Computed goto ("labels as values") is a GNU extension supported by GCC and Clang.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#else
#define MINI_COMPUTED_GOTO 0
#endif

void VM::execute() {
    // Hot state lives in locals; it is written back to the frame (SYNC) before
    // anything that can fail or inspect the call stack.
    Frame* frame = &frames_.back();
    const int32_t* ip = frame->ip;
    Value* base = stack_.data() + frame->base;
    Value* sp = base + frame->fn->nlocals;
    const Value* consts = P.consts.data();

#define SYNC() (frame->ip = ip)
#define FAIL(msg) \
    do {          \
        SYNC();   \
        fail(msg); \
    } while (0)

#if MINI_COMPUTED_GOTO
    static void* const kLabels[] = {
        &&L_OP_CONST, &&L_OP_NIL, &&L_OP_LOAD, &&L_OP_STORE, &&L_OP_GLOAD, &&L_OP_GSTORE, &&L_OP_POP,
        &&L_OP_ADD, &&L_OP_SUB, &&L_OP_MUL, &&L_OP_DIV, &&L_OP_MOD, &&L_OP_NEG, &&L_OP_NOT,
        &&L_OP_EQ, &&L_OP_NE, &&L_OP_LT, &&L_OP_LE, &&L_OP_GT, &&L_OP_GE,
        &&L_OP_JMP, &&L_OP_JZ, &&L_OP_JNZ, &&L_OP_CALL, &&L_OP_BUILTIN, &&L_OP_RET,
        &&L_OP_ARRAY, &&L_OP_INDEX, &&L_OP_SETINDEX, &&L_OP_PRINT, &&L_OP_ECHO, &&L_OP_HALT,
    };
    static_assert(sizeof(kLabels) / sizeof(kLabels[0]) == OP__COUNT, "dispatch table out of sync");
#define NEXT() goto* kLabels[*ip++]
#define TARGET(op) L_##op
    NEXT();
#else
#define NEXT() continue
#define TARGET(op) case op
    for (;;) {
        switch (*ip++) {
#endif

    TARGET(OP_CONST): {
        assign(*sp++, consts[*ip++]);
        NEXT();
    }
    TARGET(OP_NIL): {
        *sp++ = Value::nil();
        NEXT();
    }
    TARGET(OP_LOAD): {
        assign(*sp++, base[*ip++]);
        NEXT();
    }
    TARGET(OP_STORE): {
        assign(base[*ip++], sp[-1]);
        NEXT();
    }
    TARGET(OP_GLOAD): {
        const int g = *ip++;
        if (!assigned_[g]) FAIL("global '" + P.globals[g] + "' used before it was assigned");
        *sp++ = globals_[g];
        NEXT();
    }
    TARGET(OP_GSTORE): {
        const int g = *ip++;
        globals_[g] = sp[-1];
        assigned_[g] = 1;
        NEXT();
    }
    TARGET(OP_POP): {
        --sp;
        NEXT();
    }

    TARGET(OP_ADD): {
        Value& a = sp[-2];
        const Value& b = sp[-1];
        if (a.isInt() && b.isInt()) {
            a.i = static_cast<int64_t>(static_cast<uint64_t>(a.i) + static_cast<uint64_t>(b.i));
        } else if (a.isStr() || b.isStr()) {
            a = Value::string(a.toString() + b.toString());
        } else if (a.isArray() && b.isArray()) {
            ArrayData joined = a.items();
            joined.insert(joined.end(), b.items().begin(), b.items().end());
            a = Value::array(std::move(joined));
        } else {
            FAIL("unsupported operand types for '+': " + operandTypes(a, b));
        }
        --sp;
        NEXT();
    }
    TARGET(OP_SUB): {
        Value& a = sp[-2];
        const Value& b = sp[-1];
        if (!a.isInt() || !b.isInt()) FAIL("unsupported operand types for '-': " + operandTypes(a, b));
        a.i = static_cast<int64_t>(static_cast<uint64_t>(a.i) - static_cast<uint64_t>(b.i));
        --sp;
        NEXT();
    }
    TARGET(OP_MUL): {
        Value& a = sp[-2];
        const Value& b = sp[-1];
        if (a.isInt() && b.isInt()) {
            a.i = static_cast<int64_t>(static_cast<uint64_t>(a.i) * static_cast<uint64_t>(b.i));
        } else if ((a.isStr() && b.isInt()) || (a.isInt() && b.isStr())) {
            const std::string& str = a.isStr() ? a.text() : b.text();
            const int64_t n = a.isInt() ? a.i : b.i;
            if (n < 0) FAIL("cannot repeat a string a negative number of times");
            if (static_cast<double>(str.size()) * static_cast<double>(n) > 1e8) FAIL("string too large");
            std::string r;
            r.reserve(str.size() * static_cast<std::size_t>(n));
            for (int64_t k = 0; k < n; ++k) r += str;
            a = Value::string(std::move(r));
        } else {
            FAIL("unsupported operand types for '*': " + operandTypes(a, b));
        }
        --sp;
        NEXT();
    }
    TARGET(OP_DIV):
    TARGET(OP_MOD): {
        const bool isDiv = ip[-1] == OP_DIV;
        Value& a = sp[-2];
        const Value& b = sp[-1];
        if (!a.isInt() || !b.isInt())
            FAIL(std::string("unsupported operand types for '") + (isDiv ? "/" : "%") + "': " + operandTypes(a, b));
        if (b.i == 0) FAIL(isDiv ? "division by zero" : "modulo by zero");
        if (a.i == std::numeric_limits<int64_t>::min() && b.i == -1) a.i = isDiv ? a.i : 0;
        else a.i = isDiv ? a.i / b.i : a.i % b.i;
        --sp;
        NEXT();
    }
    TARGET(OP_NEG): {
        Value& a = sp[-1];
        if (!a.isInt()) FAIL(std::string("cannot negate a value of type '") + typeName(a.type) + "'");
        a.i = static_cast<int64_t>(0 - static_cast<uint64_t>(a.i));
        NEXT();
    }
    TARGET(OP_NOT): {
        setInt(sp[-1], !sp[-1].truthy());
        NEXT();
    }

    TARGET(OP_EQ):
    TARGET(OP_NE): {
        const bool eq = valuesEqual(sp[-2], sp[-1]);
        setInt(sp[-2], ip[-1] == OP_EQ ? eq : !eq);
        --sp;
        NEXT();
    }
    TARGET(OP_LT):
    TARGET(OP_LE):
    TARGET(OP_GT):
    TARGET(OP_GE): {
        const int32_t op = ip[-1];
        Value& a = sp[-2];
        const Value& b = sp[-1];
        int c = 0;
        if (a.isInt() && b.isInt()) c = (a.i > b.i) - (a.i < b.i);
        else if (a.isStr() && b.isStr()) c = a.text().compare(b.text());
        else FAIL(std::string("cannot compare ") + operandTypes(a, b) + " with '" + opSymbol(op) + "'");
        const bool r = op == OP_LT ? c < 0 : op == OP_LE ? c <= 0 : op == OP_GT ? c > 0 : c >= 0;
        setInt(a, r);
        --sp;
        NEXT();
    }

    TARGET(OP_JMP): {
        ip = frame->fn->code.data() + *ip;
        NEXT();
    }
    TARGET(OP_JZ): {
        const int32_t target = *ip++;
        --sp;
        if (!sp->truthy()) ip = frame->fn->code.data() + target;
        NEXT();
    }
    TARGET(OP_JNZ): {
        const int32_t target = *ip++;
        --sp;
        if (sp->truthy()) ip = frame->fn->code.data() + target;
        NEXT();
    }

    TARGET(OP_CALL): {
        const Function* f = &P.funcs[*ip++];
        SYNC();
        if (frames_.size() >= kMaxFrames) fail("stack overflow (more than " + std::to_string(kMaxFrames) + " nested calls)");
        const std::size_t newBase = static_cast<std::size_t>(sp - stack_.data()) - static_cast<std::size_t>(f->arity);
        const std::size_t needed = newBase + static_cast<std::size_t>(f->nlocals + f->maxStack) + 1;
        if (needed > stack_.size()) {
            ensureStack(needed);  // reallocates: re-derive pointers below
        }
        Value* locals = stack_.data() + newBase;
        for (int k = f->arity; k < f->nlocals; ++k) locals[k] = Value::nil();
        frames_.push_back({f, f->code.data(), newBase});
        frame = &frames_.back();
        ip = f->code.data();
        base = locals;
        sp = base + f->nlocals;
        NEXT();
    }
    TARGET(OP_RET): {
        {  // inner scope: clang forbids computed goto out of a scope with destructors
            Value result = std::move(sp[-1]);
            const std::size_t oldBase = frame->base;
            frames_.pop_back();
            frame = &frames_.back();
            ip = frame->ip;
            base = stack_.data() + frame->base;
            sp = stack_.data() + oldBase;
            *sp++ = std::move(result);
        }
        NEXT();
    }
    TARGET(OP_BUILTIN): {
        const int id = *ip++;
        const int argc = *ip++;
        SYNC();
        Value* args = sp - argc;
        {
            Value result = builtin(id, args, argc);
            sp = args;
            *sp++ = std::move(result);
        }
        NEXT();
    }

    TARGET(OP_ARRAY): {
        const int n = *ip++;
        Value* first = sp - n;
        {
            ArrayData items(std::make_move_iterator(first), std::make_move_iterator(sp));
            sp = first;
            *sp++ = Value::array(std::move(items));
        }
        NEXT();
    }
    TARGET(OP_INDEX): {
        const Value& obj = sp[-2];
        const Value& idx = sp[-1];
        if (obj.isArray() && idx.isInt() && idx.i >= 0 && static_cast<uint64_t>(idx.i) < obj.items().size()) {
            {
                const Value v = obj.items()[static_cast<std::size_t>(idx.i)];  // copy first: obj is overwritten
                --sp;
                sp[-1] = v;
            }
            NEXT();
        }
        SYNC();
        {
            Value result = index(sp[-2], sp[-1]);
            --sp;
            sp[-1] = std::move(result);
        }
        NEXT();
    }
    TARGET(OP_SETINDEX): {
        const Value& obj = sp[-3];
        const Value& idx = sp[-2];
        if (obj.isArray() && idx.isInt() && idx.i >= 0 && static_cast<uint64_t>(idx.i) < obj.items().size()) {
            assign((obj.items())[static_cast<std::size_t>(idx.i)], sp[-1]);
            sp[-3] = std::move(sp[-1]);
            sp -= 2;
            NEXT();
        }
        SYNC();
        setIndex(sp[-3], sp[-2], sp[-1]);
        sp[-3] = std::move(sp[-1]);
        sp -= 2;
        NEXT();
    }

    TARGET(OP_PRINT): {
        const int n = *ip++;
        Value* first = sp - n;
        for (int k = 0; k < n; ++k) {
            if (k) out_ << ' ';
            out_ << first[k].toString();
        }
        out_ << '\n';
        sp = first;
        NEXT();
    }
    TARGET(OP_ECHO): {
        --sp;
        if (!sp->isNil()) out_ << sp->repr() << '\n';
        NEXT();
    }
    TARGET(OP_HALT): {
        frames_.clear();
        for (Value* v = stack_.data(); v < sp; ++v) *v = Value::nil();  // release references
        return;
    }

#if !MINI_COMPUTED_GOTO
        default:
            FAIL("invalid opcode " + std::to_string(ip[-1]));
        }
    }
#endif
#undef NEXT
#undef TARGET
#undef FAIL
#undef SYNC
}

#if MINI_COMPUTED_GOTO
#pragma GCC diagnostic pop
#endif

}  // namespace mini
