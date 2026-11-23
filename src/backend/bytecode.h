#pragma once
#include "backend/value.h"

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace mini {

// Stack-machine instruction set. Operands follow the opcode inline in the code array.
enum Op : int32_t {
    OP_CONST,     // [k]        push constant k
    OP_NIL,       //            push nil
    OP_LOAD,      // [slot]     push local
    OP_STORE,     // [slot]     local = top (value stays on the stack)
    OP_GLOAD,     // [g]        push global
    OP_GSTORE,    // [g]        global = top (value stays on the stack)
    OP_POP,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_NEG, OP_NOT,
    OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE,
    OP_JMP,       // [target]
    OP_JZ,        // [target]   pop; jump if falsy
    OP_JNZ,       // [target]   pop; jump if truthy
    OP_CALL,      // [fn]       call; arguments are already on the stack
    OP_BUILTIN,   // [id][argc] call a built-in function
    OP_RET,       //            return top of stack to the caller
    OP_ARRAY,     // [n]        pop n values, push a new array
    OP_INDEX,     //            pop index, pop object, push object[index]
    OP_SETINDEX,  //            pop value, index, object; object[index] = value; push value
    OP_PRINT,     // [n]        pop n values and print them
    OP_ECHO,      //            REPL: pop and print repr unless nil
    OP_HALT,
    OP__COUNT
};

struct OpInfo {
    const char* name;
    int operands;
};
const OpInfo& opInfo(int32_t op);

struct Function {
    std::string name;
    int arity = 0;
    int nlocals = 0;            // parameters + local variables
    int maxStack = 0;           // max temporaries above the locals (computed by the compiler)
    std::vector<int32_t> code;
    std::vector<int> lines;     // source line of every code word, for error messages
};

struct Program {
    std::vector<Value> consts;
    std::vector<Function> funcs;
    std::vector<std::string> globals;
};

void disassemble(const Program& program, std::ostream& out, std::size_t firstFunction = 0);

}  // namespace mini
