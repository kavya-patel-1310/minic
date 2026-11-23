#include "driver/repl.h"
#include "backend/compiler.h"
#include "backend/vm.h"
#include "driver/pipeline.h"

#include <string>

namespace mini {

namespace {

// Net bracket depth outside of strings and comments; > 0 means the input continues.
int openBrackets(const std::string& text) {
    int depth = 0;
    bool inString = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (inString) {
            if (c == '\\') ++i;
            else if (c == '"') inString = false;
            continue;
        }
        if (c == '"') inString = true;
        else if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            while (i < text.size() && text[i] != '\n') ++i;
        } else if (c == '(' || c == '{' || c == '[') ++depth;
        else if (c == ')' || c == '}' || c == ']') --depth;
    }
    return depth;
}

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

void help(std::ostream& out) {
    out << "Enter MiniLang statements or expressions. Expressions are evaluated and printed.\n"
           "A trailing ';' is optional. Multi-line input continues while brackets are open.\n"
           "Commands:\n"
           "  :help     show this message\n"
           "  :dump     show the bytecode compiled so far\n"
           "  :quit     exit (Ctrl-D also works)\n";
}

}  // namespace

int runRepl(std::istream& in, std::ostream& out, std::ostream& err, bool optimize, bool color) {
    out << "MiniLang " << kVersion << " REPL. Type :help for help, :quit to exit.\n";

    Diagnostics bootstrap("<repl>", "");
    Compiler compiler(&bootstrap);
    VM vm(compiler.program(), out, in);
    int inputNumber = 0;

    for (;;) {
        out << ">>> " << std::flush;
        std::string line;
        if (!std::getline(in, line)) {
            out << "\n";
            break;
        }
        std::string source = line;
        while (openBrackets(source) > 0) {
            out << "... " << std::flush;
            if (!std::getline(in, line)) break;
            source += "\n" + line;
        }

        const std::string cmd = trim(source);
        if (cmd.empty()) continue;
        if (cmd == ":quit" || cmd == ":q" || cmd == ":exit") break;
        if (cmd == ":help") {
            help(out);
            continue;
        }
        if (cmd == ":dump") {
            disassemble(compiler.program(), out);
            continue;
        }
        if (cmd.back() != ';' && cmd.back() != '}') source += ";";

        ++inputNumber;
        Diagnostics diag("<repl:" + std::to_string(inputNumber) + ">", source);
        std::vector<StmtPtr> ast;
        // Mark expression statements for echo before optimising, so `1 + 2` still prints 3.
        if (runFrontend(source, diag, false, ast)) {
            for (auto& s : ast)
                if (s->kind == StmtKind::ExprStmt && s->expr->kind != ExprKind::Assign &&
                    s->expr->kind != ExprKind::IndexAssign)
                    s->echo = true;
            if (optimize) mini::optimize(ast);
        }
        if (diag.hasErrors()) {
            diag.print(err, color);
            continue;
        }

        compiler.setDiagnostics(&diag);
        const std::string chunkName = "<repl:" + std::to_string(inputNumber) + ">";
        const int entry = compiler.compile(ast, chunkName);
        if (entry < 0) {
            diag.print(err, color);
            continue;
        }
        try {
            vm.run(entry);
        } catch (const RuntimeError& e) {
            printRuntimeError(err, chunkName, e, color);
        }
    }
    return kOk;
}

}  // namespace mini
