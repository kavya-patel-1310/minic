#include "driver/pipeline.h"
#include "backend/compiler.h"
#include "backend/vm.h"
#include "backend/x86_codegen.h"
#include "frontend/lexer.h"
#include "frontend/parser.h"

#include <fstream>
#include <sstream>

namespace mini {

bool runFrontend(const std::string& source, Diagnostics& diag, bool optimize, std::vector<StmtPtr>& ast,
                 OptimizerStats* stats) {
    std::vector<Token> tokens;
    try {
        tokens = lex(source);
    } catch (const CompileError& e) {
        diag.error(e);
        return false;
    }
    Parser parser(std::move(tokens), diag);
    ast = parser.parseProgram();
    if (diag.hasErrors()) return false;
    if (optimize) {
        OptimizerStats s = mini::optimize(ast);
        if (stats) *stats = s;
    }
    return true;
}

int runSource(const std::string& filename, const std::string& source, bool optimize, std::ostream& out,
              std::istream& in, std::ostream& err, bool color) {
    Diagnostics diag(filename, source);
    std::vector<StmtPtr> ast;
    if (!runFrontend(source, diag, optimize, ast)) {
        diag.print(err, color);
        return kCompileError;
    }
    Compiler compiler(&diag);
    const int entry = compiler.compile(ast);
    if (entry < 0) {
        diag.print(err, color);
        return kCompileError;
    }
    VM vm(compiler.program(), out, in);
    try {
        vm.run(entry);
    } catch (const RuntimeError& e) {
        out.flush();
        printRuntimeError(err, filename, e, color);
        return kRuntimeError;
    }
    return kOk;
}

bool compileToAssembly(const std::string& filename, const std::string& source, bool optimize, std::string& asmOut,
                       std::ostream& err, bool color) {
    Diagnostics diag(filename, source);
    std::vector<StmtPtr> ast;
    if (!runFrontend(source, diag, optimize, ast)) {
        diag.print(err, color);
        return false;
    }
    // Run the bytecode compiler first: it performs the full set of semantic checks.
    Compiler checker(&diag);
    if (checker.compile(ast) < 0) {
        diag.print(err, color);
        return false;
    }
    std::ostringstream out;
    X86Codegen codegen(diag);
    if (!codegen.generate(ast, out)) {
        diag.print(err, color);
        return false;
    }
    asmOut = out.str();
    return true;
}

bool readFile(const std::string& path, std::string& contents) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::stringstream buffer;
    buffer << file.rdbuf();
    contents = buffer.str();
    return true;
}

}  // namespace mini
