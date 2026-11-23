// Unit tests for every compiler stage: lexer, parser, optimizer, bytecode
// compiler, virtual machine and the native code generator.
#include "test_framework.h"

#include "backend/compiler.h"
#include "backend/vm.h"
#include "backend/x86_codegen.h"
#include "driver/pipeline.h"
#include "frontend/lexer.h"
#include "frontend/parser.h"
#include "middle/optimizer.h"

#include <sstream>

using namespace mini;

namespace {

struct Result {
    int status;
    std::string out;
    std::string err;
};

Result run(const std::string& src, bool optimize = true, const std::string& input = "") {
    std::ostringstream out, err;
    std::istringstream in(input);
    int status = runSource("test.mini", src, optimize, out, in, err);
    return {status, out.str(), err.str()};
}

// Parses `src` (optionally optimizing) and returns the S-expression of its statements.
std::string ast(const std::string& src, bool optimize = false) {
    Diagnostics diag("test.mini", src);
    std::vector<StmtPtr> program;
    if (!runFrontend(src, diag, optimize, program)) return "<error: " + diag.messages().front() + ">";
    std::ostringstream os;
    printAst(program, os);
    std::string s = os.str();
    if (!s.empty() && s.back() == '\n') s.pop_back();
    return s;
}

std::vector<std::string> compileErrors(const std::string& src) {
    Diagnostics diag("test.mini", src);
    std::vector<StmtPtr> program;
    if (runFrontend(src, diag, true, program)) {
        Compiler compiler(&diag);
        compiler.compile(program);
    }
    return diag.messages();
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

// ===================================================================== lexer

TEST("lexer: keywords, identifiers and operators") {
    auto toks = lex("let x = foo += 1 <= 2 && !y;");
    std::vector<Tok> expected = {Tok::Let, Tok::Ident, Tok::Assign, Tok::Ident, Tok::PlusAssign, Tok::Int,
                                 Tok::Le, Tok::Int, Tok::AndAnd, Tok::Bang, Tok::Ident, Tok::Semi, Tok::End};
    CHECK_EQ(toks.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) CHECK(toks[i].type == expected[i]);
    CHECK_EQ(toks[3].text, std::string("foo"));
}

TEST("lexer: line and column tracking") {
    auto toks = lex("let a = 1;\n  print a;");
    CHECK_EQ(toks[5].loc.line, 2);  // print
    CHECK_EQ(toks[5].loc.col, 3);
    CHECK_EQ(toks[6].loc.col, 9);   // a
}

TEST("lexer: string escapes") {
    auto toks = lex(R"("a\tb\n\"q\"\\")");
    CHECK_EQ(toks[0].text, std::string("a\tb\n\"q\"\\"));
}

TEST("lexer: comments are skipped") {
    auto toks = lex("// nothing here\n42 // trailing");
    CHECK_EQ(toks.size(), std::size_t(2));
    CHECK_EQ(toks[0].value, int64_t(42));
}

TEST("lexer: errors") {
    auto fails = [](const std::string& src, const std::string& msg) {
        try {
            lex(src);
        } catch (const CompileError& e) {
            return contains(e.what(), msg);
        }
        return false;
    };
    CHECK(fails("\"open", "unterminated string"));
    CHECK(fails("a & b", "'&&'"));
    CHECK(fails("x = $", "unexpected character"));
    CHECK(fails("99999999999999999999", "64 bits"));
    CHECK(fails("12abc", "invalid number"));
}

// ==================================================================== parser

TEST("parser: operator precedence") {
    CHECK_EQ(ast("1 + 2 * 3 - 4;"), std::string("(expr (- (+ 1 (* 2 3)) 4))"));
    CHECK_EQ(ast("a || b && c == d < e;"), std::string("(expr (|| a (&& b (== c (< d e)))))"));
    CHECK_EQ(ast("-a * !b;"), std::string("(expr (* (- a) (! b)))"));
    CHECK_EQ(ast("(1 + 2) * 3;"), std::string("(expr (* (+ 1 2) 3))"));
}

TEST("parser: assignment is right associative") {
    CHECK_EQ(ast("a = b = 3;"), std::string("(expr (= a (= b 3)))"));
}

TEST("parser: compound assignment desugars") {
    CHECK_EQ(ast("x += 2;"), std::string("(expr (= x (+ x 2)))"));
    CHECK_EQ(ast("a[i] *= 3;"), std::string("(expr (= (index a i) (* (index a i) 3)))"));
}

TEST("parser: arrays, indexing and calls") {
    CHECK_EQ(ast("f([1, 2,], m[0][1]);"), std::string("(expr (call f (array 1 2) (index (index m 0) 1)))"));
}

TEST("parser: for loop") {
    CHECK_EQ(ast("for (let i = 0; i < 3; i += 1) print i;"),
             std::string("(for\n  (let i 0)\n  (< i 3)\n  (= i (+ i 1))\n  (print i)\n)"));
}

TEST("parser: reports multiple errors with recovery") {
    Diagnostics diag("t", "");
    std::vector<StmtPtr> program;
    runFrontend("let x = ;\nprint 1;\nlet = 3;\nprint (2;\n", diag, false, program);
    CHECK_EQ(diag.errorCount(), std::size_t(3));
}

TEST("parser: invalid assignment target") {
    CHECK(contains(ast("1 = 2;"), "invalid assignment target"));
    CHECK(contains(ast("f() = 2;"), "invalid assignment target"));
}

TEST("parser: functions only at top level") {
    CHECK(contains(ast("fn a() { fn b() {} }"), "top level"));
}

// ================================================================= optimizer

TEST("optimizer: folds integer arithmetic") {
    CHECK_EQ(ast("print 2 * 3 + 4, -(5 - 10), 7 % 4;", true), std::string("(print 10 5 3)"));
}

TEST("optimizer: folds strings and comparisons") {
    CHECK_EQ(ast("print \"a\" + 1 + \"b\", \"x\" == \"x\", 1 < 2, \"a\" == 1;", true),
             std::string("(print \"a1b\" 1 1 0)"));
}

TEST("optimizer: does not fold division by zero") {
    CHECK_EQ(ast("print 1 / 0;", true), std::string("(print (/ 1 0))"));
}

TEST("optimizer: short-circuit folding keeps side effects") {
    CHECK_EQ(ast("print 0 && f(), 1 || f(), 1 && f();", true), std::string("(print 0 1 (&& 1 (call f)))"));
}

TEST("optimizer: removes constant branches") {
    CHECK_EQ(ast("if (1 > 2) print 1; else print 2;", true), std::string("(block\n  (print 2)\n)"));
    CHECK_EQ(ast("while (false) print 1;", true), std::string(""));
}

TEST("optimizer: removes dead code after return") {
    CHECK_EQ(ast("fn f() { return 1; print 2; print 3; }", true), std::string("(fn f ()\n  (return 1)\n)"));
}

TEST("optimizer: reports statistics") {
    Diagnostics diag("t", "");
    std::vector<StmtPtr> program;
    OptimizerStats stats;
    runFrontend("print 1 + 2; if (0) print 3; fn f() { return 1; print 4; }", diag, true, program, &stats);
    CHECK_EQ(stats.constantsFolded, 1);
    CHECK_EQ(stats.branchesRemoved, 1);
    CHECK_EQ(stats.deadStatements, 1);
}

// ========================================================== semantic checks

TEST("compiler: undefined names") {
    CHECK(contains(compileErrors("print y;").at(0), "undefined variable 'y'"));
    CHECK(contains(compileErrors("y = 1;").at(0), "undeclared variable 'y'"));
    CHECK(contains(compileErrors("g();").at(0), "undefined function 'g'"));
}

TEST("compiler: arity checks") {
    CHECK(contains(compileErrors("fn f(a, b) {} f(1);").at(0), "expects 2 arguments but got 1"));
    CHECK(contains(compileErrors("len(1, 2);").at(0), "len(string | array)"));
}

TEST("compiler: break/continue/return placement") {
    CHECK(contains(compileErrors("break;").at(0), "'break' outside of a loop"));
    CHECK(contains(compileErrors("continue;").at(0), "'continue' outside of a loop"));
    CHECK(contains(compileErrors("return 1;").at(0), "outside of a function"));
}

TEST("compiler: redefinitions") {
    CHECK(contains(compileErrors("fn f() {} fn f() {}").at(0), "already defined"));
    CHECK(contains(compileErrors("fn len(x) {}").at(0), "built-in"));
    CHECK(contains(compileErrors("fn f() { let a = 1; let a = 2; }").at(0), "already declared"));
    CHECK(contains(compileErrors("fn f(a, a) {}").at(0), "duplicate parameter"));
}

TEST("compiler: collects several semantic errors") {
    CHECK_EQ(compileErrors("print a; print b; fn f() { print c; }").size(), std::size_t(3));
}

TEST("compiler: functions can be used before definition") {
    CHECK(compileErrors("print f(); fn f() { return 1; }").empty());
}

// ======================================================================= VM

TEST("vm: arithmetic and printing") {
    CHECK_EQ(run("print 1 + 2 * 3, 7 / 2, -7 / 2, 7 % 3, -7 % 3;").out, std::string("7 3 -3 1 -1\n"));
}

TEST("vm: integer overflow wraps") {
    CHECK_EQ(run("let m = 9223372036854775807; print m + 1;").out, std::string("-9223372036854775808\n"));
}

TEST("vm: strings") {
    CHECK_EQ(run("let s = \"ab\"; print s + 1, s * 3, len(s), s[1], s[-1], s < \"b\";").out,
             std::string("ab1 ababab 2 b b 1\n"));
}

TEST("vm: arrays have reference semantics") {
    CHECK_EQ(run("let a = [1, 2]; let b = a; push(b, 3); b[0] = 9; print a, len(a), [1] + [2];").out,
             std::string("[9, 2, 3] 3 [1, 2]\n"));
}

TEST("vm: nested arrays and repr") {
    CHECK_EQ(run("print [1, \"two\", [3, nil]];").out, std::string("[1, \"two\", [3, nil]]\n"));
}

TEST("vm: equality") {
    CHECK_EQ(run("let a = [1]; print 1 == 1, \"a\" == \"a\", nil == nil, 1 == \"1\", a == a, [1] == [1];").out,
             std::string("1 1 1 0 1 0\n"));
}

TEST("vm: logical operators short-circuit") {
    CHECK_EQ(run("let n = 0; fn bump() { n += 1; return 1; } let r = 0 && bump(); r = 1 || bump(); print n, r;").out,
             std::string("0 1\n"));
}

TEST("vm: loops with break and continue") {
    const char* src =
        "let out = [];"
        "for (let i = 0; i < 10; i += 1) { if (i % 2 == 0) continue; if (i > 7) break; push(out, i); }"
        "let j = 0; while (true) { j += 1; if (j == 5) break; }"
        "print out, j;";
    CHECK_EQ(run(src).out, std::string("[1, 3, 5, 7] 5\n"));
}

TEST("vm: block scoping and shadowing") {
    CHECK_EQ(run("let x = 1; { let x = 2; print x; } print x;").out, std::string("2\n1\n"));
    CHECK_EQ(run("fn f() { let a = 1; { let a = a + 10; print a; } return a; } print f();").out,
             std::string("11\n1\n"));
}

TEST("vm: recursion and globals") {
    CHECK_EQ(run("let calls = 0; fn fact(n) { calls += 1; if (n <= 1) return 1; return n * fact(n - 1); }"
                 "print fact(20), calls;").out,
             std::string("2432902008176640000 20\n"));
}

TEST("vm: functions return nil by default") {
    CHECK_EQ(run("fn f() {} print f(), type(f());").out, std::string("nil nil\n"));
}

TEST("vm: built-ins") {
    const char* src =
        "let a = array(3, 7); push(a, 1); print len(a), a; print pop(a), len(a);"
        "print str(12) + \"!\", int(\" -5 \") * 2, type(1), type(\"s\"), abs(-4);";
    CHECK_EQ(run(src).out, std::string("4 [7, 7, 7, 1]\n1 3\n12! -10 int string 4\n"));
}

TEST("vm: input reads lines until EOF") {
    CHECK_EQ(run("let a = input(); let b = input(); let c = input(); print a + b, c;", true, "4\n2\n").out,
             std::string("42 nil\n"));
}

TEST("vm: runtime errors carry line numbers and a stack trace") {
    Result r = run("fn f(x) {\n  return 10 / x;\n}\nprint f(0);");
    CHECK_EQ(r.status, 2);
    CHECK(contains(r.err, "test.mini:2: runtime error: division by zero"));
    CHECK(contains(r.err, "at f() line 2"));
    CHECK(contains(r.err, "at <script> line 4"));
}

TEST("vm: runtime type errors") {
    CHECK(contains(run("print 1 - \"a\";").err, "unsupported operand types for '-': 'int' and 'string'"));
    CHECK(contains(run("print [1][5];").err, "index 5 out of range for length 1"));
    CHECK(contains(run("let s = \"x\"; s[0] = \"y\";").err, "strings are immutable"));
    CHECK(contains(run("print int(\"12x\");").err, "cannot parse"));
    CHECK(contains(run("assert(1 == 2, \"math\");").err, "assertion failed: math"));
    CHECK(contains(run("print pop([]);").err, "empty array"));
}

TEST("vm: stack overflow is caught") {
    Result r = run("fn f(n) { return f(n + 1); } f(0);");
    CHECK_EQ(r.status, 2);
    CHECK(contains(r.err, "stack overflow"));
    CHECK(contains(r.err, "more frames"));
}

TEST("vm: output before a runtime error is kept") {
    Result r = run("print \"before\"; print 1 / 0;");
    CHECK_EQ(r.out, std::string("before\n"));
    CHECK_EQ(r.status, 2);
}

// ======================================================= optimizer is safe

TEST("differential: -O0 and -O1 produce identical output") {
    const char* programs[] = {
        "print 2 * 3 + 4 * (5 - 1), \"x\" + 2 + 3, 10 / 3 * 3;",
        "if (1) { let a = 5; print a; } else print 0;",
        "fn f(n) { if (n < 2) return n; return f(n - 1) + f(n - 2); return 99; print 1; } print f(15);",
        "for (let i = 0; 0; i += 1) print i; let i = 3; print i;",
        "print !0, !\"\", !\"a\", -(-5), 1 && 2, 0 || \"\";",
    };
    for (const char* p : programs) {
        Result o0 = run(p, false), o1 = run(p, true);
        CHECK_EQ(o1.out, o0.out);
        CHECK_EQ(o1.status, o0.status);
    }
}

// ============================================================ incremental

TEST("compiler: incremental compilation and rollback (REPL support)") {
    Diagnostics d1("r1", "");
    Compiler compiler(&d1);
    std::ostringstream out;
    std::istringstream in;
    VM vm(compiler.program(), out, in);

    auto chunk = [&](const std::string& src, Diagnostics& d) {
        std::vector<StmtPtr> program;
        if (!runFrontend(src, d, true, program)) return -1;
        compiler.setDiagnostics(&d);
        return compiler.compile(program);
    };

    int e1 = chunk("let x = 40; fn add(a, b) { return a + b; }", d1);
    CHECK(e1 >= 0);
    vm.run(e1);

    Diagnostics d2("r2", "");
    CHECK_EQ(chunk("fn broken() { return nope; }", d2), -1);  // rolled back

    Diagnostics d3("r3", "");
    int e3 = chunk("fn broken() { return 2; } print add(x, broken());", d3);  // name is free again
    CHECK(e3 >= 0);
    vm.run(e3);
    CHECK_EQ(out.str(), std::string("42\n"));
}

// ============================================================ native backend

namespace {
std::string nativeAsm(const std::string& src, std::string* error = nullptr) {
    std::string out;
    std::ostringstream err;
    if (!compileToAssembly("test.mini", src, true, out, err)) {
        if (error) *error = err.str();
        return "";
    }
    return out;
}
}  // namespace

TEST("native: emits functions, main and runtime support") {
    std::string s = nativeAsm("fn sq(x) { return x * x; } print sq(7);");
    CHECK(contains(s, "mini_sq:"));
    CHECK(contains(s, ".globl main"));
    CHECK(contains(s, "imul %rcx, %rax"));
    CHECK(contains(s, "call mini_sq"));
    CHECK(contains(s, "mini_rt_div0:"));
}

TEST("native: comparisons in conditions fuse into cmp + jcc") {
    std::string s = nativeAsm("let i = 0; while (i < 10) i += 1;");
    CHECK(contains(s, "cmp %rcx, %rax\n    jge"));
    CHECK(!contains(s, "setl"));
}

TEST("native: division checks for zero") {
    std::string s = nativeAsm("let a = 5; let b = 0; print a / b;");
    CHECK(contains(s, "call mini_rt_div0"));
    CHECK(contains(s, "idiv %rcx"));
}

TEST("native: string literals are only allowed in print") {
    CHECK(contains(nativeAsm("print \"hi\", 1;"), ".asciz \"hi\""));
    std::string err;
    nativeAsm("let s = \"hi\";", &err);
    CHECK(contains(err, "not supported by the native backend"));
}

TEST("native: rejects dynamic features with clear errors") {
    std::string err;
    nativeAsm("let a = [1];\nprint len(\"x\");\nprint a[0];\nprint nil;", &err);
    CHECK(contains(err, "arrays are not supported"));
    CHECK(contains(err, "built-in function 'len'"));
    CHECK(contains(err, "indexing"));
    CHECK(contains(err, "'nil'"));
}

TEST("native: more than six parameters is an error") {
    std::string err;
    nativeAsm("fn f(a, b, c, d, e, f2, g) { return a; } print f(1,2,3,4,5,6,7);", &err);
    CHECK(contains(err, "at most 6 parameters"));
}

int main() { return test::runAll(); }
