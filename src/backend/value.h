#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace mini {

enum class ValueType : uint8_t { Nil, Int, Str, Array };

struct Value;
using ArrayData = std::vector<Value>;

// Heap objects use intrusive, non-atomic reference counting. The VM is single
// threaded, so this avoids the atomic increments std::shared_ptr would pay on
// every copy. (Like CPython without its cycle collector, an array that contains
// itself is never freed.)
struct Object {
    uint32_t refs = 1;
    ValueType type;
    explicit Object(ValueType t) : type(t) {}
};
struct StringObject : Object {
    explicit StringObject(std::string v) : Object(ValueType::Str), value(std::move(v)) {}
    const std::string value;  // strings are immutable
};
struct ArrayObject : Object {
    explicit ArrayObject(ArrayData v) : Object(ValueType::Array), items(std::move(v)) {}
    ArrayData items;  // arrays are mutable and shared by reference
};

// A dynamically typed MiniLang value: nil, a 64-bit integer stored inline,
// or a reference to a heap object (string or array).
struct Value {
    ValueType type = ValueType::Nil;
    int64_t i = 0;
    Object* obj = nullptr;

    Value() = default;
    Value(const Value& o) : type(o.type), i(o.i), obj(o.obj) {
        if (obj) ++obj->refs;
    }
    Value(Value&& o) noexcept : type(o.type), i(o.i), obj(o.obj) {
        o.type = ValueType::Nil;
        o.obj = nullptr;
    }
    Value& operator=(const Value& o) {
        if (o.obj) ++o.obj->refs;  // increment first: safe for self-assignment
        release();
        type = o.type;
        i = o.i;
        obj = o.obj;
        return *this;
    }
    Value& operator=(Value&& o) noexcept {
        if (this != &o) {
            release();
            type = o.type;
            i = o.i;
            obj = o.obj;
            o.type = ValueType::Nil;
            o.obj = nullptr;
        }
        return *this;
    }
    ~Value() { release(); }

    static Value nil() { return Value{}; }
    static Value integer(int64_t v) {
        Value x;
        x.type = ValueType::Int;
        x.i = v;
        return x;
    }
    static Value string(std::string v);
    static Value array(ArrayData items);

    bool isInt() const { return type == ValueType::Int; }
    bool isStr() const { return type == ValueType::Str; }
    bool isArray() const { return type == ValueType::Array; }
    bool isNil() const { return type == ValueType::Nil; }

    const std::string& text() const { return static_cast<const StringObject*>(obj)->value; }
    ArrayData& items() const { return static_cast<ArrayObject*>(obj)->items; }

    bool truthy() const;
    std::string toString() const;  // as printed by `print`
    std::string repr() const;      // strings quoted, as shown inside arrays and by the REPL

private:
    void release() {
        if (obj && --obj->refs == 0) destroy(obj);
        obj = nullptr;
    }
    static void destroy(Object* o);
};

const char* typeName(ValueType t);
bool valuesEqual(const Value& a, const Value& b);

}  // namespace mini
