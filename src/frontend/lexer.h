#pragma once
#include "common/diagnostics.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mini {

enum class Tok {
    // literals and names
    Int, Str, Ident,
    // keywords
    Let, Fn, If, Else, While, For, Break, Continue, Return, Print, True, False, Nil,
    // operators
    Plus, Minus, Star, Slash, Percent, Bang, Assign,
    PlusAssign, MinusAssign, StarAssign, SlashAssign, PercentAssign,
    Eq, Ne, Lt, Le, Gt, Ge, AndAnd, OrOr,
    // punctuation
    LParen, RParen, LBrace, RBrace, LBracket, RBracket, Comma, Semi,
    End
};

struct Token {
    Tok type = Tok::End;
    std::string text;   // identifier, string contents, or operator spelling
    int64_t value = 0;  // integer literal value
    SourceLoc loc;
};

// Converts source text to tokens. Throws CompileError on malformed input.
std::vector<Token> lex(const std::string& source);

const char* tokName(Tok t);

}  // namespace mini
