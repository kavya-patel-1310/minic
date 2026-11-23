#include "common/diagnostics.h"

#if defined(_WIN32)
#include <io.h>
#define MINI_ISATTY(fd) _isatty(fd)
#else
#include <unistd.h>
#define MINI_ISATTY(fd) isatty(fd)
#endif

namespace mini {

namespace {
struct Palette {
    const char* bold;
    const char* red;
    const char* green;
    const char* reset;
};
Palette palette(bool color) {
    if (color) return {"\033[1m", "\033[1;31m", "\033[1;32m", "\033[0m"};
    return {"", "", "", ""};
}
}  // namespace

Diagnostics::Diagnostics(std::string filename, const std::string& source) : filename_(std::move(filename)) {
    std::string current;
    for (char c : source) {
        if (c == '\n') {
            lines_.push_back(current);
            current.clear();
        } else if (c != '\r') {
            current += c;
        }
    }
    lines_.push_back(current);
}

void Diagnostics::error(const std::string& message, SourceLoc loc) {
    if (entries_.size() >= kMaxErrors) {
        truncated_ = true;
        return;
    }
    entries_.push_back({message, loc});
}

std::vector<std::string> Diagnostics::messages() const {
    std::vector<std::string> out;
    for (const auto& e : entries_) out.push_back(e.message);
    return out;
}

void Diagnostics::print(std::ostream& out, bool color) const {
    const Palette c = palette(color);
    for (const Entry& e : entries_) {
        out << c.bold << filename_ << ":" << e.loc.line << ":" << e.loc.col << ": " << c.reset
            << c.red << "error: " << c.reset << c.bold << e.message << c.reset << "\n";

        if (e.loc.line < 1 || e.loc.line > static_cast<int>(lines_.size())) continue;
        const std::string& text = lines_[e.loc.line - 1];
        const std::string num = std::to_string(e.loc.line);
        out << " " << num << " | " << text << "\n";
        out << " " << std::string(num.size(), ' ') << " | ";
        for (int i = 0; i < e.loc.col - 1 && i < static_cast<int>(text.size()); ++i)
            out << (text[i] == '\t' ? '\t' : ' ');  // keep tabs so the caret lines up
        out << c.green << "^" << c.reset << "\n";
    }
    if (truncated_) out << "too many errors; stopping after " << kMaxErrors << "\n";
    const std::size_t n = entries_.size();
    out << n << (n == 1 ? " error" : " errors") << " generated.\n";
}

bool stderrIsTerminal() { return MINI_ISATTY(2) != 0; }

void printRuntimeError(std::ostream& out, const std::string& filename, const RuntimeError& e, bool color) {
    const Palette c = palette(color);
    out << c.bold << filename << ":" << e.line << ": " << c.reset << c.red << "runtime error: " << c.reset
        << c.bold << e.what() << c.reset << "\n";
    for (const std::string& frame : e.trace) out << "    at " << frame << "\n";
}

}  // namespace mini
