#include "backend/builtins.h"

namespace mini {

namespace {
// Order must match the Builtin enum.
const BuiltinInfo kBuiltins[] = {
    {"len", 1, 1, "len(string | array) -> int"},
    {"push", 2, 2, "push(array, value) -> nil"},
    {"pop", 1, 1, "pop(array) -> value"},
    {"str", 1, 1, "str(value) -> string"},
    {"int", 1, 1, "int(string | int) -> int"},
    {"input", 0, 1, "input([prompt]) -> string | nil"},
    {"clock", 0, 0, "clock() -> int (milliseconds)"},
    {"assert", 1, 2, "assert(condition, [message]) -> nil"},
    {"type", 1, 1, "type(value) -> string"},
    {"abs", 1, 1, "abs(int) -> int"},
    {"array", 1, 2, "array(size, [fill]) -> array"},
};
static_assert(sizeof(kBuiltins) / sizeof(kBuiltins[0]) == static_cast<int>(Builtin::Count),
              "builtin table out of sync with enum");
}  // namespace

const BuiltinInfo& builtinInfo(int id) { return kBuiltins[id]; }

int findBuiltin(const std::string& name) {
    for (int k = 0; k < builtinCount(); ++k)
        if (name == kBuiltins[k].name) return k;
    return -1;
}

}  // namespace mini
