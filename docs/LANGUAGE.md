# MiniLang language reference

MiniLang is a small, dynamically typed, imperative language with C-like syntax.

## Lexical structure

- **Comments:** `//` to the end of the line.
- **Identifiers:** `[A-Za-z_][A-Za-z0-9_]*`
- **Keywords:** `let fn if else while for break continue return print true false nil`
- **Integers:** decimal, 64-bit signed (`0`, `42`, `9223372036854775807`). `true` and `false` are `1` and `0`.
- **Strings:** double-quoted, with escapes `\n \t \r \0 \" \\`. Strings may span lines.

## Types

| Type | Examples | Notes |
|---|---|---|
| `int` | `0`, `-17`, `true` | 64-bit signed; overflow wraps around (two's complement) |
| `string` | `"hi"` | Immutable |
| `array` | `[1, "a", [2]]` | Mutable, heterogeneous, shared by reference |
| `nil` | `nil` | Returned by functions without a `return` value |

**Truthiness:** `nil`, `0` and `""` are false. Every other value, including every array, is true.

## Operators (lowest to highest precedence)

| Precedence | Operators | Notes |
|---|---|---|
| 1 | `=  +=  -=  *=  /=  %=` | Right associative; an assignment is an expression and yields its value |
| 2 | `\|\|` | Short-circuit; result is `0` or `1` |
| 3 | `&&` | Short-circuit; result is `0` or `1` |
| 4 | `==  !=` | Values of different types are never equal; arrays compare by identity |
| 5 | `<  <=  >  >=` | Two ints, or two strings (lexicographic) |
| 6 | `+  -` | `+` also concatenates strings (`"a" + 1` is `"a1"`) and arrays |
| 7 | `*  /  %` | `/` truncates toward zero; `"ab" * 3` is `"ababab"` |
| 8 | `-  !` (unary) | |
| 9 | `a[i]` | Arrays and strings; negative indices count from the end |

Division or modulo by zero is a runtime error.

## Statements

```js
let x = expr;                       // declare (an initializer is required)
x = expr;  a[i] = expr;  x += 1;    // assign
print a, b, c;                      // print values separated by spaces, then a newline
if (cond) stmt else stmt
while (cond) stmt
for (init; cond; step) stmt         // each part is optional; init may be a `let`
break;  continue;
{ ... }                             // block (introduces a scope)
fn name(params) { ... }             // function (top level only)
return expr;  return;
```

## Scoping

- A `let` at the top level of the script declares a **global**. Globals are visible inside every function.
- Any other `let` declares a **local** that lasts until the end of its block. The body of an `if`, `while` or `for` is always its own scope.
- A local can shadow an outer variable. The initializer is evaluated before the new name exists, so `let x = x + 1;` reads the outer `x`.
- Functions can be called before their definition. Functions are not values.

## Built-in functions

| Function | Description |
|---|---|
| `len(x)` | Length of a string or array |
| `push(arr, v)` | Append `v` to `arr` |
| `pop(arr)` | Remove and return the last element |
| `str(v)` | Convert any value to its printed form |
| `int(s)` | Parse a string as an integer (surrounding whitespace allowed) |
| `input([prompt])` | Read one line from stdin; returns `nil` at end of file |
| `clock()` | Milliseconds since the program started |
| `assert(cond, [msg])` | Raise a runtime error if `cond` is false |
| `type(v)` | `"int"`, `"string"`, `"array"` or `"nil"` |
| `abs(n)` | Absolute value |
| `array(n, [fill])` | New array of `n` elements, each `fill` (default `0`) |

## Native backend subset

`minic build` compiles programs that use only:
- integer variables (globals and locals) and integer arithmetic, comparisons and logic
- `if`, `while`, `for`, `break`, `continue`, blocks
- functions with up to 6 parameters, including recursion
- `print` with integer expressions and string literals (including constant-folded concatenations of literals)

Anything else is rejected at compile time with an error pointing at the construct. Such programs still run on the VM with `minic run`.

## Grammar (EBNF)

```ebnf
program     = { function | statement } EOF ;
function    = "fn" IDENT "(" [ IDENT { "," IDENT } ] ")" block ;
block       = "{" { statement } "}" ;

statement   = letStmt | printStmt | ifStmt | whileStmt | forStmt
            | "break" ";" | "continue" ";" | returnStmt | block | expression ";" ;
letStmt     = "let" IDENT "=" expression ";" ;
printStmt   = "print" expression { "," expression } ";" ;
ifStmt      = "if" "(" expression ")" statement [ "else" statement ] ;
whileStmt   = "while" "(" expression ")" statement ;
forStmt     = "for" "(" ( letStmt | expression ";" | ";" )
                        [ expression ] ";" [ expression ] ")" statement ;
returnStmt  = "return" [ expression ] ";" ;

expression  = assignment ;
assignment  = ( IDENT | postfix "[" expression "]" ) assignOp assignment | logicOr ;
assignOp    = "=" | "+=" | "-=" | "*=" | "/=" | "%=" ;
logicOr     = logicAnd { "||" logicAnd } ;
logicAnd    = equality { "&&" equality } ;
equality    = comparison { ( "==" | "!=" ) comparison } ;
comparison  = term { ( "<" | "<=" | ">" | ">=" ) term } ;
term        = factor { ( "+" | "-" ) factor } ;
factor      = unary { ( "*" | "/" | "%" ) unary } ;
unary       = ( "-" | "!" ) unary | postfix ;
postfix     = primary { "[" expression "]" } ;
primary     = INT | STRING | "true" | "false" | "nil"
            | IDENT [ "(" [ expression { "," expression } ] ")" ]
            | "[" [ expression { "," expression } [ "," ] ] "]"
            | "(" expression ")" ;
```
