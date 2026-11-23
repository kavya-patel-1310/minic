// minic: the MiniLang compiler driver.
#include "backend/compiler.h"
#include "backend/vm.h"
#include "driver/pipeline.h"
#include "driver/repl.h"
#include "frontend/lexer.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>

using namespace mini;

namespace {

struct Options {
    std::string command = "run";
    std::string input;
    std::string output;
    bool optimize = true;
    bool asmOnly = false;
    bool color = stderrIsTerminal();
};

void usage(std::ostream& out) {
    out << "minic " << kVersion << " - compiler for the MiniLang programming language\n\n"
        << "usage: minic [command] [options] <file.mini>\n\n"
        << "commands:\n"
        << "  run      compile to bytecode and execute on the VM (default)\n"
        << "  build    compile to a native x86-64 executable (Linux; needs cc)\n"
        << "  dump     print the compiled bytecode\n"
        << "  ast      print the syntax tree\n"
        << "  tokens   print the token stream\n"
        << "  repl     start an interactive session (also: minic with no arguments)\n\n"
        << "options:\n"
        << "  -O0          disable the optimizer\n"
        << "  -o <file>    output path for build (default: input name without extension)\n"
        << "  -S           build: emit assembly (<output>.s) and stop\n"
        << "  --no-color   disable colored diagnostics\n"
        << "  --version    print version\n"
        << "  -h, --help   show this message\n";
}

bool isCommand(const std::string& s) {
    return s == "run" || s == "build" || s == "dump" || s == "ast" || s == "tokens" || s == "repl";
}

std::string defaultOutput(const std::string& input) {
    std::string base = input;
    const auto slash = base.find_last_of("/\\");
    if (slash != std::string::npos) base = base.substr(slash + 1);
    const auto dot = base.find_last_of('.');
    if (dot != std::string::npos && dot > 0) base = base.substr(0, dot);
    return base.empty() ? "a.out" : base;
}

std::string shellQuote(const std::string& s) {
    std::string out = "'";
    for (char c : s) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return out + "'";
}

int build(const Options& opt, const std::string& source) {
    std::string assembly;
    if (!compileToAssembly(opt.input, source, opt.optimize, assembly, std::cerr, opt.color)) return kCompileError;

    const std::string output = opt.output.empty() ? defaultOutput(opt.input) : opt.output;
    const std::string asmPath = output + ".s";
    {
        std::ofstream f(asmPath);
        if (!f || !(f << assembly)) {
            std::cerr << "error: cannot write '" << asmPath << "'\n";
            return kIoError;
        }
    }
    if (opt.asmOnly) return kOk;

    const char* cc = std::getenv("CC");
    const std::string cmd = std::string(cc && *cc ? cc : "cc") + " -o " + shellQuote(output) + " " + shellQuote(asmPath);
    const int status = std::system(cmd.c_str());
    std::remove(asmPath.c_str());
    if (status != 0) {
        std::cerr << "error: assembling/linking failed (command: " << cmd << ")\n";
        return kToolError;
    }
    return kOk;
}

}  // namespace

int main(int argc, char** argv) {
    std::ios::sync_with_stdio(false);
    Options opt;
    bool commandGiven = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            usage(std::cout);
            return kOk;
        }
        if (arg == "--version") {
            std::cout << "minic " << kVersion << "\n";
            return kOk;
        }
        if (arg == "-O0") opt.optimize = false;
        else if (arg == "-O1" || arg == "-O") opt.optimize = true;
        else if (arg == "-S") opt.asmOnly = true;
        else if (arg == "--no-color") opt.color = false;
        else if (arg == "-o") {
            if (i + 1 >= argc) {
                std::cerr << "error: -o needs a file name\n";
                return kUsage;
            }
            opt.output = argv[++i];
        } else if (!commandGiven && opt.input.empty() && isCommand(arg)) {
            opt.command = arg;
            commandGiven = true;
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "error: unknown option '" << arg << "'\n\n";
            usage(std::cerr);
            return kUsage;
        } else if (opt.input.empty()) {
            opt.input = arg;
        } else {
            std::cerr << "error: only one input file is allowed\n";
            return kUsage;
        }
    }

    if (opt.command == "repl" || (opt.input.empty() && !commandGiven))
        return runRepl(std::cin, std::cout, std::cerr, opt.optimize, opt.color);
    if (opt.input.empty()) {
        std::cerr << "error: no input file\n\n";
        usage(std::cerr);
        return kUsage;
    }

    std::string source;
    if (!readFile(opt.input, source)) {
        std::cerr << "error: cannot open '" << opt.input << "'\n";
        return kIoError;
    }

    if (opt.command == "run") return runSource(opt.input, source, opt.optimize, std::cout, std::cin, std::cerr, opt.color);
    if (opt.command == "build") return build(opt, source);

    if (opt.command == "tokens") {
        try {
            for (const Token& t : lex(source)) {
                std::cout << t.loc.line << ":" << t.loc.col << "\t" << tokName(t.type);
                if (t.type == Tok::Int || t.type == Tok::Ident) std::cout << "\t" << t.text;
                if (t.type == Tok::Str) std::cout << "\t\"" << t.text << "\"";
                std::cout << "\n";
            }
        } catch (const CompileError& e) {
            Diagnostics diag(opt.input, source);
            diag.error(e);
            diag.print(std::cerr, opt.color);
            return kCompileError;
        }
        return kOk;
    }

    // ast / dump
    Diagnostics diag(opt.input, source);
    std::vector<StmtPtr> ast;
    OptimizerStats stats;
    if (!runFrontend(source, diag, opt.optimize, ast, &stats)) {
        diag.print(std::cerr, opt.color);
        return kCompileError;
    }
    if (opt.command == "ast") {
        printAst(ast, std::cout);
        return kOk;
    }
    Compiler compiler(&diag);
    if (compiler.compile(ast) < 0) {
        diag.print(std::cerr, opt.color);
        return kCompileError;
    }
    if (opt.optimize)
        std::cout << "; optimizer: " << stats.constantsFolded << " constant(s) folded, " << stats.branchesRemoved
                  << " branch(es) removed, " << stats.deadStatements << " dead statement(s) removed\n\n";
    disassemble(compiler.program(), std::cout);
    return kOk;
}
