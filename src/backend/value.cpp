#include "backend/value.h"

namespace mini {

Value Value::string(std::string v) {
    Value x;
    x.type = ValueType::Str;
    x.obj = new StringObject(std::move(v));
    return x;
}

Value Value::array(ArrayData items) {
    Value x;
    x.type = ValueType::Array;
    x.obj = new ArrayObject(std::move(items));
    return x;
}

void Value::destroy(Object* o) {
    if (o->type == ValueType::Str) delete static_cast<StringObject*>(o);
    else delete static_cast<ArrayObject*>(o);
}

const char* typeName(ValueType t) {
    switch (t) {
        case ValueType::Nil: return "nil";
        case ValueType::Int: return "int";
        case ValueType::Str: return "string";
        case ValueType::Array: return "array";
    }
    return "?";
}

bool Value::truthy() const {
    switch (type) {
        case ValueType::Nil: return false;
        case ValueType::Int: return i != 0;
        case ValueType::Str: return !text().empty();
        case ValueType::Array: return true;
    }
    return false;
}

namespace {

std::string quoted(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            default: out += c;
        }
    }
    return out + "\"";
}

std::string render(const Value& v, bool quoteStrings, int depth) {
    switch (v.type) {
        case ValueType::Nil: return "nil";
        case ValueType::Int: return std::to_string(v.i);
        case ValueType::Str: return quoteStrings ? quoted(v.text()) : v.text();
        case ValueType::Array: {
            if (depth > 32) return "[...]";  // guards against self-referencing arrays
            std::string out = "[";
            for (std::size_t k = 0; k < v.items().size(); ++k) {
                if (k) out += ", ";
                out += render(v.items()[k], true, depth + 1);
            }
            return out + "]";
        }
    }
    return "?";
}

}  // namespace

std::string Value::toString() const { return render(*this, false, 0); }
std::string Value::repr() const { return render(*this, true, 0); }

bool valuesEqual(const Value& a, const Value& b) {
    if (a.type != b.type) return false;
    switch (a.type) {
        case ValueType::Nil: return true;
        case ValueType::Int: return a.i == b.i;
        case ValueType::Str: return a.text() == b.text();
        case ValueType::Array: return a.obj == b.obj;  // identity
    }
    return false;
}

}  // namespace mini
