#pragma once
#include <cstddef>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace mini {

struct SourceLoc {
    int line = 0;
    int col = 0;
};

// Thrown by the lexer, parser, compiler and code generator.
class CompileError : public std::runtime_error {
public:
    CompileError(const std::string& message, SourceLoc where) : std::runtime_error(message), loc(where) {}
    SourceLoc loc;
};

// Thrown by the virtual machine.
class RuntimeError : public std::runtime_error {
public:
    RuntimeError(const std::string& message, int where) : std::runtime_error(message), line(where) {}
    int line;
    std::vector<std::string> trace;  // call stack, innermost frame first
};

// Collects compile errors and renders them clang-style, with the offending
// source line and a caret under the exact column.
class Diagnostics {
public:
    static constexpr std::size_t kMaxErrors = 25;

    Diagnostics(std::string filename, const std::string& source);

    void error(const std::string& message, SourceLoc loc);
    void error(const CompileError& e) { error(e.what(), e.loc); }

    bool hasErrors() const { return !entries_.empty(); }
    std::size_t errorCount() const { return entries_.size(); }
    std::vector<std::string> messages() const;

    void print(std::ostream& out, bool color) const;

private:
    struct Entry {
        std::string message;
        SourceLoc loc;
    };
    std::string filename_;
    std::vector<std::string> lines_;
    std::vector<Entry> entries_;
    bool truncated_ = false;
};

bool stderrIsTerminal();
void printRuntimeError(std::ostream& out, const std::string& filename, const RuntimeError& e, bool color);

}  // namespace mini
