# minic architecture and design notes

```
source ─► Lexer ─► Parser ─► Optimizer ─┬─► Bytecode compiler ─► VM
                                         └─► x86-64 codegen ─► cc ─► executable
```

Every stage is a library component (`minic_core`). The `minic` binary, the REPL and the unit tests all drive the same pipeline (`src/driver/pipeline.cpp`).

## 1. Diagnostics (`src/common`)

Every token and AST node carries a `SourceLoc {line, col}`. Compile errors are `CompileError` exceptions that the parser and compiler *catch and record* in a `Diagnostics` object, then keep going. `Diagnostics::print` renders them clang-style, with the source line and a caret. Tabs are kept in the caret line so it still lines up.

Runtime errors are `RuntimeError` exceptions carrying the source line and a stack trace that the VM builds from its call frames. Very deep recursion is abbreviated after 12 frames.

## 2. Lexer (`src/frontend/lexer.cpp`)

A single pass over the source text, tracking line and column (from the start of the line). Two-character operators (`<=`, `+=`, `&&`, ...) are recognised by peeking one character ahead. Integer literals are range-checked, and a literal running into letters (`12abc`) is rejected.

## 3. Parser (`src/frontend/parser.cpp`)

A recursive-descent parser with one function per precedence level. All left-associative binary levels share one helper that takes a member-function pointer to the next level down.

- **Desugaring:** `a += b` becomes `a = a + b`, and `a[i] op= b` becomes `a[i] = a[i] op b`.
- **Error recovery:** when a statement fails to parse, the error is recorded and `synchronize()` skips to a likely statement boundary: after a `;`, or before a keyword or `}`. If skipping reaches a `{`, the whole block is skipped, because it belongs to the construct whose header failed. Without that, a broken `fn f(a {` would produce a cascade of bogus errors from its body. A flag also suppresses a stray `}` reported right after a recovery.
- **Missing `;` or `)`:** these are reported at the end of the previous token when the next token is on a new line, which is where the user forgot it.

The AST (`ast.h`) uses tagged structs owned by `std::unique_ptr`, and has an S-expression printer (`minic ast`) that the unit tests use to assert on parse trees.

## 4. Optimizer (`src/middle/optimizer.cpp`)

A bottom-up pass over the AST:

| Transformation | Example |
|---|---|
| Integer folding (wrapping semantics, as in the VM) | `2 * 3 + 4` → `10` |
| String folding | `"a" + 1 + "b"` → `"a1b"` |
| Comparison and equality folding | `"x" == "x"` → `1`, `1 == "1"` → `0` |
| Short-circuit folding | `0 && f()` → `0`; `1 && f()` is kept, because `f()` must still run |
| Constant branches | `if (0) A else B` → `{ B }`; `while (0) ...` → removed |
| Dead code | statements after `return`, `break` or `continue` in a block |
| Useless statements | `1 + 2;` → removed |

**Safety rules:** operations that would fail at run time (`1 / 0`) are never folded, so the error still happens at the same place. A surviving branch is wrapped in a block so its `let` declarations keep their scope. Differential tests check that `-O0` and `-O1` give identical output.

## 5. Bytecode compiler (`src/backend/compiler.cpp`)

The compiler lowers the AST to a stack-machine instruction set and does the semantic checks.

**Three passes per compilation unit:**
1. Declare all functions (name → index, arity) and top-level globals, so code can call a function defined later.
2. Compile the function bodies.
3. Compile the top-level script into an entry function.

**Variables.** Each function has a scope stack (`vector<unordered_map<string,int>>`) mapping names to frame slots. Slot numbers are reused when a block ends, and `Function::nlocals` records the high-water mark. An empty scope stack means "top level of the script", where `let` creates a global. RAII guards (`ScopeGuard`, `LoopGuard`) keep the stacks balanced when a `CompileError` unwinds.

**Control flow.** Forward jumps are emitted with a placeholder and back-patched. Each loop keeps lists of pending `break` and `continue` jumps. In a `for` loop, `continue` targets the step expression rather than the condition.

**Short-circuit logic** compiles to jumps:
```
a && b:   a; JZ F; b; JZ F; CONST 1; JMP E; F: CONST 0; E:
```

**Static stack-depth analysis.** Every emitted instruction updates a simulated stack depth using its stack effect (for example, `CALL f` is `1 - arity(f)` and `ARRAY n` is `1 - n`). The maximum is stored in `Function::maxStack`. Where two control-flow paths join (the logical-operator pattern), the depth is adjusted so it doesn't drift upward.

**Incremental compilation.** `compile()` appends to the same `Program`. It snapshots the symbol tables first and rolls back if the chunk has errors. This is what lets the REPL keep definitions across inputs: a failed input leaves no half-defined function behind.

### Instruction set

| Opcode | Operands | Stack effect | Meaning |
|---|---|---|---|
| `CONST` | k | +1 | push constant k |
| `NIL` | | +1 | push nil |
| `LOAD` / `STORE` | slot | +1 / 0 | read or write a local (`STORE` keeps the value on the stack) |
| `GLOAD` / `GSTORE` | g | +1 / 0 | read or write a global |
| `POP` | | -1 | |
| `ADD SUB MUL DIV MOD` | | -1 | arithmetic, plus string and array `+` and string `*` |
| `EQ NE LT LE GT GE` | | -1 | comparisons |
| `NEG NOT` | | 0 | |
| `JMP` / `JZ` / `JNZ` | target | 0 / -1 / -1 | jumps |
| `CALL` | fn | 1 - arity | call a user function |
| `BUILTIN` | id, argc | 1 - argc | call a built-in |
| `RET` | | | return the top of the stack |
| `ARRAY` | n | 1 - n | build an array from n values |
| `INDEX` / `SETINDEX` | | -1 / -2 | `a[i]` / `a[i] = v` |
| `PRINT` | n | -n | print n values |
| `ECHO` | | -1 | REPL: print the repr of the value unless it is nil |
| `HALT` | | | end of a script chunk |

## 6. Virtual machine (`src/backend/vm.cpp`)

**Values** (`value.h`) are a 24-byte struct: a type tag, an inline `int64_t`, and a pointer to a heap object (`StringObject` or `ArrayObject`). Heap objects use **intrusive, non-atomic reference counts**. The VM is single threaded, so this avoids the atomic increments `std::shared_ptr` performs on every copy. Switching cut the sieve benchmark from 1.45 s to 0.82 s. A fast-path `assign` copies ints and nils without touching any reference count.

**Stack and frames.** There is one contiguous `vector<Value>`. A frame records its function, its saved `ip` and its `base`. Parameters are the caller's pushed arguments, so calls copy no arguments. Locals follow the parameters, and temporaries sit above them. On `CALL`, the VM checks once that `base + nlocals + maxStack` fits and grows the stack if needed; after that, every push is unchecked.

**Dispatch.** The hot loop keeps `ip`, `sp`, `base` and the constant table in locals. It uses **computed goto** (a GCC/Clang extension) with a portable `switch` fallback. Each handler jumps straight to the next one, which gives the branch predictor one indirect branch per opcode. Clang forbids an indirect goto from leaving a scope with destructors, so handlers with temporary `Value`s use an inner block.

**Errors.** `SYNC()` writes `ip` back to the frame before anything that can fail. `fail()` maps each frame's `ip - 1` back to a source line, which works because every word of an instruction records the same line, to build the stack trace.

**Limits.** Recursion depth is capped at 10,000 frames, which turns runaway recursion into a "stack overflow" error instead of a crash. Integer overflow wraps, and `INT64_MIN / -1` is defined, matching the native backend.

## 7. Native x86-64 backend (`src/backend/x86_codegen.cpp`)

The native backend works directly on the AST and emits GNU assembler (AT&T syntax) for the System V AMD64 ABI. The generated code is position-independent: RIP-relative data, `@PLT` calls and `stderr@GOTPCREL`. It links against libc with the system `cc`.

**Model.** Every expression leaves its result in `%rax`. Locals and parameters live in `%rbp`-relative slots (`-8*(slot+1)(%rbp)`). Globals go in `.bss` as `mini_g_<name>`, and functions are named `mini_<name>` to avoid clashing with libc. The script body becomes `main`.

**Calling convention.** Arguments are evaluated left to right and pushed, then popped into `%rdi, %rsi, %rdx, %rcx, %r8, %r9`. The callee's prologue stores them into its first slots. That's why there's a limit of six parameters.

**Stack alignment.** The ABI requires `%rsp` to be 16-byte aligned at every `call`. The frame size is rounded up to 16, and the generator counts outstanding pushes (`depth_`). When that count is odd, it brackets the call with `sub $8, %rsp` and `add $8, %rsp`.

**Code-quality optimizations:**
- *Direct operands:* if the right operand of a binary operator is a literal or a variable, it's loaded straight into `%rcx`, which avoids a push and pop.
- *Compare-and-branch fusion:* a comparison used as an `if`, `while` or `for` condition compiles to `cmp` plus an inverted `jcc`, instead of `setcc`, `movzbl` and `test`. `&&` and `!` in conditions are lowered to jump chains.

**Runtime support** is a few lines of assembly. `mini_rt_div0` aligns the stack, prints `runtime error: division by zero (line N)` to stderr, and exits with code 2, the same as the VM. `INT64_MIN / -1` would trap with `idiv`, so it's special-cased to wrap.

**Scope.** Strings, arrays, `nil` and built-ins need a runtime library, so they're rejected with a precise diagnostic. The bytecode compiler runs first as the semantic checker, so native builds get exactly the same error checking as the VM.

## 8. Testing strategy

| Layer | Location | What it guarantees |
|---|---|---|
| Unit tests | `tests/unit/unit_tests.cpp` | Each stage on its own: tokens and columns, parse trees (through the S-expression printer), folding results, semantic errors, VM semantics, stack traces, incremental compilation and rollback, native code patterns |
| Golden tests | `tests/golden/*.out` | Every example's exact output, with and without the optimizer |
| Differential tests | `tests/run_tests.sh` | Native executables match the VM byte for byte; `-O0` matches `-O1` |
| Error tests | `tests/errors/*.mini` | Exit code and message for compile and runtime failures |
| Sanitizers | `make sanitize`, CI | No memory errors, undefined behaviour or leaks across the whole suite |

The test framework is a small header (`tests/unit/test_framework.h`) with self-registering `TEST(...)` cases and `CHECK` / `CHECK_EQ`, so the project has no third-party dependencies.
