#pragma once
#include "backend/bytecode.h"
#include "common/diagnostics.h"

#include <chrono>
#include <istream>
#include <ostream>

namespace mini {

// Stack-based bytecode interpreter.
//
// Design:
//   * One contiguous value stack. Each call frame owns a window of it: parameters
//     and locals occupy fixed slots starting at the frame's base, temporaries sit
//     above them.
//   * The stack is preallocated. The compiler computes every function's maximum
//     temporary depth (Function::maxStack), so the VM checks for room once per call
//     instead of on every push.
//   * The hot loop keeps the instruction pointer, stack pointer and frame base in
//     local variables and uses computed-goto dispatch on GCC/Clang (a portable
//     switch elsewhere).
//   * Global state persists across run() calls, which is what the REPL relies on.
class VM {
public:
    VM(const Program& program, std::ostream& out, std::istream& in);

    // Runs function `entry` (a script chunk). Throws RuntimeError on failure.
    void run(int entry);

    static constexpr std::size_t kMaxFrames = 10000;

private:
    struct Frame {
        const Function* fn;
        const int32_t* ip;  // saved instruction pointer (only up to date for non-top frames)
        std::size_t base;   // index of the frame's first local in stack_
    };

    const Program& P;
    std::ostream& out_;
    std::istream& in_;
    std::vector<Value> stack_;
    std::vector<Frame> frames_;
    std::vector<Value> globals_;
    std::vector<char> assigned_;
    std::chrono::steady_clock::time_point start_;

    void execute();
    void ensureStack(std::size_t needed);
    Value builtin(int id, Value* args, int argc);
    Value index(const Value& object, const Value& idx);
    void setIndex(const Value& object, const Value& idx, Value v);
    [[noreturn]] void fail(const std::string& message);
};

}  // namespace mini
