#include "backend/bytecode.h"
#include "backend/builtins.h"

#include <iomanip>

namespace mini {

const OpInfo& opInfo(int32_t op) {
    static const OpInfo table[] = {
        {"CONST", 1}, {"NIL", 0},   {"LOAD", 1},  {"STORE", 1}, {"GLOAD", 1}, {"GSTORE", 1}, {"POP", 0},
        {"ADD", 0},   {"SUB", 0},   {"MUL", 0},   {"DIV", 0},   {"MOD", 0},   {"NEG", 0},    {"NOT", 0},
        {"EQ", 0},    {"NE", 0},    {"LT", 0},    {"LE", 0},    {"GT", 0},    {"GE", 0},
        {"JMP", 1},   {"JZ", 1},    {"JNZ", 1},   {"CALL", 1},  {"BUILTIN", 2}, {"RET", 0},
        {"ARRAY", 1}, {"INDEX", 0}, {"SETINDEX", 0}, {"PRINT", 1}, {"ECHO", 0}, {"HALT", 0},
    };
    static_assert(sizeof(table) / sizeof(table[0]) == OP__COUNT, "opcode table out of sync");
    static const OpInfo invalid{"???", 0};
    return (op >= 0 && op < OP__COUNT) ? table[op] : invalid;
}

void disassemble(const Program& P, std::ostream& out, std::size_t firstFunction) {
    for (std::size_t fi = firstFunction; fi < P.funcs.size(); ++fi) {
        const Function& f = P.funcs[fi];
        out << "== " << f.name << "  (arity " << f.arity << ", locals " << f.nlocals << ", "
            << f.code.size() << " words) ==\n";
        int lastLine = -1;
        for (std::size_t ip = 0; ip < f.code.size();) {
            const int32_t op = f.code[ip];
            const OpInfo& info = opInfo(op);
            const int line = f.lines[ip];

            out << std::setw(5) << std::setfill('0') << ip << std::setfill(' ') << "  ";
            if (line == lastLine) out << "   | ";
            else out << std::setw(4) << line << " ";
            lastLine = line;
            out << std::left << std::setw(9) << info.name << std::right;

            if (info.operands >= 1) out << std::setw(4) << f.code[ip + 1];
            if (info.operands >= 2) out << " " << f.code[ip + 2];
            if (info.operands >= 1) {
                const int32_t a = f.code[ip + 1];
                switch (op) {
                    case OP_CONST: out << "    ; " << P.consts[a].repr(); break;
                    case OP_GLOAD:
                    case OP_GSTORE: out << "    ; " << P.globals[a]; break;
                    case OP_CALL: out << "    ; " << P.funcs[a].name << "()"; break;
                    case OP_BUILTIN: out << "  ; " << builtinInfo(a).name << "()"; break;
                    default: break;
                }
            }
            out << "\n";
            ip += 1 + info.operands;
        }
        out << "\n";
    }
}

}  // namespace mini
