#include "frontend/lexer.h"

#include <cctype>
#include <unordered_map>

namespace mini {

const char* tokName(Tok t) {
    switch (t) {
        case Tok::Int: return "INT";
        case Tok::Str: return "STRING";
        case Tok::Ident: return "IDENT";
        case Tok::Let: return "let";
        case Tok::Fn: return "fn";
        case Tok::If: return "if";
        case Tok::Else: return "else";
        case Tok::While: return "while";
        case Tok::For: return "for";
        case Tok::Break: return "break";
        case Tok::Continue: return "continue";
        case Tok::Return: return "return";
        case Tok::Print: return "print";
        case Tok::True: return "true";
        case Tok::False: return "false";
        case Tok::Nil: return "nil";
        case Tok::Plus: return "+";
        case Tok::Minus: return "-";
        case Tok::Star: return "*";
        case Tok::Slash: return "/";
        case Tok::Percent: return "%";
        case Tok::Bang: return "!";
        case Tok::Assign: return "=";
        case Tok::PlusAssign: return "+=";
        case Tok::MinusAssign: return "-=";
        case Tok::StarAssign: return "*=";
        case Tok::SlashAssign: return "/=";
        case Tok::PercentAssign: return "%=";
        case Tok::Eq: return "==";
        case Tok::Ne: return "!=";
        case Tok::Lt: return "<";
        case Tok::Le: return "<=";
        case Tok::Gt: return ">";
        case Tok::Ge: return ">=";
        case Tok::AndAnd: return "&&";
        case Tok::OrOr: return "||";
        case Tok::LParen: return "(";
        case Tok::RParen: return ")";
        case Tok::LBrace: return "{";
        case Tok::RBrace: return "}";
        case Tok::LBracket: return "[";
        case Tok::RBracket: return "]";
        case Tok::Comma: return ",";
        case Tok::Semi: return ";";
        case Tok::End: return "EOF";
    }
    return "?";
}

namespace {

class Lexer {
public:
    explicit Lexer(const std::string& src) : s_(src) {}

    std::vector<Token> run() {
        while (i_ < s_.size()) scanToken();
        tokens_.push_back({Tok::End, "end of file", 0, here()});
        return std::move(tokens_);
    }

private:
    const std::string& s_;
    std::size_t i_ = 0;
    std::size_t lineStart_ = 0;
    int line_ = 1;
    std::vector<Token> tokens_;

    SourceLoc here() const { return {line_, static_cast<int>(i_ - lineStart_) + 1}; }
    bool next(char c) const { return i_ + 1 < s_.size() && s_[i_ + 1] == c; }
    void newline() {
        ++line_;
        lineStart_ = i_ + 1;
    }

    void add(Tok t, SourceLoc loc, std::size_t length) {
        tokens_.push_back({t, std::string(tokName(t)), 0, loc});
        i_ += length;
    }

    // Emits `plain` or, if the next character is '=', `withEq`.
    void addMaybeEq(Tok plain, Tok withEq, SourceLoc loc) {
        if (next('=')) add(withEq, loc, 2);
        else add(plain, loc, 1);
    }

    void scanToken() {
        const char c = s_[i_];
        const SourceLoc loc = here();

        if (c == '\n') { newline(); ++i_; return; }
        if (std::isspace(static_cast<unsigned char>(c))) { ++i_; return; }
        if (c == '/' && next('/')) {
            while (i_ < s_.size() && s_[i_] != '\n') ++i_;
            return;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) return number(loc);
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') return identifier(loc);
        if (c == '"') return string(loc);

        switch (c) {
            case '+': return addMaybeEq(Tok::Plus, Tok::PlusAssign, loc);
            case '-': return addMaybeEq(Tok::Minus, Tok::MinusAssign, loc);
            case '*': return addMaybeEq(Tok::Star, Tok::StarAssign, loc);
            case '/': return addMaybeEq(Tok::Slash, Tok::SlashAssign, loc);
            case '%': return addMaybeEq(Tok::Percent, Tok::PercentAssign, loc);
            case '=': return addMaybeEq(Tok::Assign, Tok::Eq, loc);
            case '!': return addMaybeEq(Tok::Bang, Tok::Ne, loc);
            case '<': return addMaybeEq(Tok::Lt, Tok::Le, loc);
            case '>': return addMaybeEq(Tok::Gt, Tok::Ge, loc);
            case '(': return add(Tok::LParen, loc, 1);
            case ')': return add(Tok::RParen, loc, 1);
            case '{': return add(Tok::LBrace, loc, 1);
            case '}': return add(Tok::RBrace, loc, 1);
            case '[': return add(Tok::LBracket, loc, 1);
            case ']': return add(Tok::RBracket, loc, 1);
            case ',': return add(Tok::Comma, loc, 1);
            case ';': return add(Tok::Semi, loc, 1);
            case '&':
                if (!next('&')) throw CompileError("expected '&&' (MiniLang has no bitwise '&')", loc);
                return add(Tok::AndAnd, loc, 2);
            case '|':
                if (!next('|')) throw CompileError("expected '||' (MiniLang has no bitwise '|')", loc);
                return add(Tok::OrOr, loc, 2);
            default:
                throw CompileError(std::string("unexpected character '") + c + "'", loc);
        }
    }

    void number(SourceLoc loc) {
        const std::size_t start = i_;
        while (i_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_]))) ++i_;
        if (i_ < s_.size() && (std::isalpha(static_cast<unsigned char>(s_[i_])) || s_[i_] == '_'))
            throw CompileError("invalid number literal", loc);
        Token t{Tok::Int, s_.substr(start, i_ - start), 0, loc};
        try {
            t.value = std::stoll(t.text);
        } catch (...) {
            throw CompileError("integer literal '" + t.text + "' does not fit in 64 bits", loc);
        }
        tokens_.push_back(t);
    }

    void identifier(SourceLoc loc) {
        static const std::unordered_map<std::string, Tok> keywords = {
            {"let", Tok::Let},       {"fn", Tok::Fn},         {"if", Tok::If},
            {"else", Tok::Else},     {"while", Tok::While},   {"for", Tok::For},
            {"break", Tok::Break},   {"continue", Tok::Continue},
            {"return", Tok::Return}, {"print", Tok::Print},   {"true", Tok::True},
            {"false", Tok::False},   {"nil", Tok::Nil}};
        const std::size_t start = i_;
        while (i_ < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[i_])) || s_[i_] == '_')) ++i_;
        std::string word = s_.substr(start, i_ - start);
        auto it = keywords.find(word);
        tokens_.push_back({it == keywords.end() ? Tok::Ident : it->second, word, 0, loc});
    }

    void string(SourceLoc loc) {
        ++i_;  // opening quote
        std::string out;
        while (i_ < s_.size() && s_[i_] != '"') {
            char ch = s_[i_];
            if (ch == '\n') newline();
            if (ch == '\\') {
                if (i_ + 1 >= s_.size()) break;
                const SourceLoc escLoc = here();
                switch (s_[++i_]) {
                    case 'n': ch = '\n'; break;
                    case 't': ch = '\t'; break;
                    case 'r': ch = '\r'; break;
                    case '0': ch = '\0'; break;
                    case '"': ch = '"'; break;
                    case '\\': ch = '\\'; break;
                    default: throw CompileError(std::string("unknown escape sequence '\\") + s_[i_] + "'", escLoc);
                }
            }
            out += ch;
            ++i_;
        }
        if (i_ >= s_.size()) throw CompileError("unterminated string literal", loc);
        ++i_;  // closing quote
        tokens_.push_back({Tok::Str, out, 0, loc});
    }
};

}  // namespace

std::vector<Token> lex(const std::string& source) { return Lexer(source).run(); }

}  // namespace mini
