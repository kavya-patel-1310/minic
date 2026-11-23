#pragma once
#include <string>

namespace mini {

enum class Builtin : int { Len, Push, Pop, Str, Int, Input, Clock, Assert, Type, Abs, Array, Count };

struct BuiltinInfo {
    const char* name;
    int minArgs;
    int maxArgs;
    const char* signature;  // for docs and error messages
};

const BuiltinInfo& builtinInfo(int id);
int findBuiltin(const std::string& name);  // -1 if `name` is not a built-in
constexpr int builtinCount() { return static_cast<int>(Builtin::Count); }

}  // namespace mini
