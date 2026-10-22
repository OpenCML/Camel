// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "values.h"

#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/other.h"
#include "camel/runtime/graph.h"

#include <charconv>
#include <ostream>
#include <streambuf>

namespace debugger::core {

using namespace camel::core::type;
using namespace camel::core::rtdata;

namespace {

/// A stream buffer that keeps the first `limit` characters and then stops the printing by
/// throwing: a large array or tensor is not formatted in full just to be cut.
struct PrintLimitReached {};

class LimitedBuffer final : public std::streambuf {
  public:
    explicit LimitedBuffer(size_t limit) : limit_(limit) {}
    const std::string &text() const { return text_; }

  protected:
    int_type overflow(int_type ch) override {
        if (ch != traits_type::eof()) {
            put(static_cast<char>(ch));
        }
        return ch;
    }
    std::streamsize xsputn(const char *s, std::streamsize n) override {
        for (std::streamsize i = 0; i < n; ++i) {
            put(s[i]);
        }
        return n;
    }

  private:
    void put(char c) {
        if (text_.size() >= limit_) {
            throw PrintLimitReached{};
        }
        text_ += c;
    }
    size_t limit_;
    std::string text_;
};

/// The program's own printing of the value, cut at `limit`.
std::string printed(slot_t slot, Type *type, size_t limit) {
    LimitedBuffer buffer(limit);
    std::ostream os(&buffer);
    os.exceptions(std::ios::badbit); // let PrintLimitReached through the stream
    bool cut = false;
    try {
        printSlot(os, slot, type);
    } catch (const PrintLimitReached &) {
        cut = true;
    } catch (const std::ios_base::failure &) {
        cut = true; // how a stream reports an exception of its buffer
    } catch (...) {
        return "<cannot be printed>";
    }
    std::string text = buffer.text();
    // Do not cut inside a UTF-8 sequence.
    while (cut && !text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80) {
        text.pop_back();
    }
    if (cut) {
        text += "...";
    }
    return text;
}

std::string quoted(const std::string &text) {
    std::string out = "'";
    for (char c : text) {
        switch (c) {
        case '\n':
            out += "\\n";
            break;
        case '\t':
            out += "\\t";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\'':
            out += "\\'";
            break;
        case '\\':
            out += "\\\\";
            break;
        default:
            out += c;
        }
    }
    out += "'";
    return out;
}

const TupleType *asTuple(const Type *type) {
    return type && type->code() == TypeCode::Tuple ? static_cast<const TupleType *>(type) : nullptr;
}

/// Looks through the forms that only wrap one value: a `var` cell and a non-null `T?` or a
/// `T!` that has not failed. Returns false when the view ends in null or in an error.
enum class Unwrapped { Value, Null, Error };
Unwrapped unwrap(ValueView &view) {
    for (int depth = 0; depth < 8; ++depth) {
        const TupleType *tuple = asTuple(view.type);
        if (!tuple) {
            return Unwrapped::Value;
        }
        if (tuple->isCell()) {
            if (view.slot == NullSlot) {
                return Unwrapped::Null;
            }
            view = {reinterpret_cast<const Tuple *>(view.slot)->get<slot_t>(0), tuple->typeAt(0)};
            continue;
        }
        if (tuple->isNullable()) {
            if (isNullValue(view.slot)) {
                return Unwrapped::Null;
            }
            view = {nullablePayload(view.slot), tuple->payloadType()};
            continue;
        }
        if (tuple->isFallible()) {
            if (view.slot == NullSlot) {
                return Unwrapped::Null;
            }
            if (isErrorValue(view.slot)) {
                return Unwrapped::Error;
            }
            view = {falliblePayload(view.slot), tuple->payloadType()};
            continue;
        }
        return Unwrapped::Value;
    }
    return Unwrapped::Value;
}

} // namespace

std::string typeText(const Type *type) {
    if (type == nullptr) {
        return "?";
    }
    std::string text = isOtherType(type->code()) ? static_cast<const OtherType *>(type)->spelling()
                                                 : type->toString();
    // The compiler names the number types by width, programs by `int` and `float`.
    static const std::pair<std::string_view, std::string_view> names[] = {
        {"i64", "int"},
        {"f64", "float"},
    };
    const auto isWord = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_';
    };
    std::string out;
    for (size_t i = 0; i < text.size();) {
        if (isWord(text[i]) && (i == 0 || !isWord(text[i - 1]))) {
            size_t end = i;
            while (end < text.size() && isWord(text[end])) {
                ++end;
            }
            std::string_view word(text.data() + i, end - i);
            bool replaced = false;
            for (const auto &[from, to] : names) {
                if (word == from) {
                    out += to;
                    replaced = true;
                    break;
                }
            }
            if (!replaced) {
                out += word;
            }
            i = end;
            continue;
        }
        out += text[i++];
    }
    return out;
}

Rendered render(const ValueView &original, size_t limit) {
    Rendered out;
    out.type = typeText(original.type);
    if (original.type == nullptr) {
        out.value = "<no type>";
        return out;
    }
    if (original.slot == DeadSlot) {
        out.value = "<not evaluated>";
        return out;
    }
    ValueView view = original;
    switch (unwrap(view)) {
    case Unwrapped::Null:
        out.value = "null";
        return out;
    case Unwrapped::Error:
        out.value      = printed(original.slot, original.type, limit);
        out.expandable = true;
        return out;
    case Unwrapped::Value:
        break;
    }
    const TypeCode code = view.type->code();
    if (isGCTraced(code) && view.slot == NullSlot) {
        out.value = "null";
        return out;
    }
    std::string text = printed(view.slot, view.type, limit);
    switch (code) {
    case TypeCode::String:
        out.value = quoted(text);
        break;
    case TypeCode::Array: {
        const size_t n = reinterpret_cast<const Array *>(view.slot)->size();
        out.value      = "(" + std::to_string(n) + ") " + text;
        out.expandable = n > 0;
    } break;
    case TypeCode::Tuple:
        out.value      = text;
        out.expandable = reinterpret_cast<const Tuple *>(view.slot)->size() > 0;
        break;
    case TypeCode::Struct:
        out.value      = text;
        out.expandable = reinterpret_cast<const Struct *>(view.slot)->size() > 0;
        break;
    case TypeCode::Function: {
        const auto *func = reinterpret_cast<const Function *>(view.slot);
        out.value        = text;
        out.expandable   = func->tuple() != nullptr && func->tuple()->size() > 0;
    } break;
    default:
        out.value = text;
        break;
    }
    return out;
}

std::vector<Member> members(const ValueView &original, size_t maxCount, size_t *total) {
    std::vector<Member> out;
    size_t count   = 0;
    ValueView view = original;
    if (view.type == nullptr || view.slot == DeadSlot) {
        return out;
    }
    switch (unwrap(view)) {
    case Unwrapped::Null:
        return out;
    case Unwrapped::Error: {
        // The Error structure of a failed `T!`: kind, message, location.
        StructType *errorStruct = errorType();
        const slot_t error      = fallibleError(view.slot);
        if (error != NullSlot && errorStruct != nullptr) {
            const auto *s = reinterpret_cast<const Struct *>(error);
            count         = std::min(s->size(), errorStruct->size());
            for (size_t i = 0; i < count && i < maxCount; ++i) {
                out.push_back(
                    {std::string(errorStruct->fieldName(i)),
                     {s->get<slot_t>(i), errorStruct->typeAt(i)}});
            }
        }
        if (total) {
            *total = count;
        }
        return out;
    }
    case Unwrapped::Value:
        break;
    }
    const TypeCode code = view.type->code();
    if (isGCTraced(code) && view.slot == NullSlot) {
        return out;
    }
    switch (code) {
    case TypeCode::Array: {
        const auto *array = reinterpret_cast<const Array *>(view.slot);
        Type *elemType    = static_cast<const ArrayType *>(view.type)->elemType();
        count             = array->size();
        for (size_t i = 0; i < count && i < maxCount; ++i) {
            out.push_back({"[" + std::to_string(i) + "]", {array->data()[i], elemType}});
        }
    } break;
    case TypeCode::Tuple: {
        const auto *tuple     = reinterpret_cast<const Tuple *>(view.slot);
        const auto *tupleType = static_cast<const TupleType *>(view.type);
        count                 = std::min(tuple->size(), tupleType->size());
        for (size_t i = 0; i < count && i < maxCount; ++i) {
            out.push_back({std::to_string(i), {tuple->get<slot_t>(i), tupleType->typeAt(i)}});
        }
    } break;
    case TypeCode::Struct: {
        const auto *s          = reinterpret_cast<const Struct *>(view.slot);
        const auto *structType = static_cast<const StructType *>(view.type);
        count                  = std::min(s->size(), structType->size());
        for (size_t i = 0; i < count && i < maxCount; ++i) {
            out.push_back(
                {std::string(structType->fieldName(i)),
                 {s->get<slot_t>(i), structType->typeAt(i)}});
        }
    } break;
    case TypeCode::Function: {
        // The captured values of a closure, under the names they were captured by.
        const auto *func    = reinterpret_cast<const Function *>(view.slot);
        const Tuple *values = func->tuple();
        const auto *graph   = func->graph();
        const auto *types   = graph ? graph->closureType() : nullptr;
        if (values == nullptr || types == nullptr) {
            break;
        }
        std::vector<std::string_view> names;
        if (graph != nullptr) {
            for (camel::core::SymId name : graph->captureNames()) {
                names.push_back(camel::core::symbolText(name));
            }
        }
        count = std::min(values->size(), types->size());
        for (size_t i = 0; i < count && i < maxCount; ++i) {
            std::string name = i < names.size() ? std::string(names[i]) : std::to_string(i);
            out.push_back({std::move(name), {values->get<slot_t>(i), types->typeAt(i)}});
        }
    } break;
    default:
        break;
    }
    if (total) {
        *total = count;
    }
    return out;
}

std::optional<ValueView> member(const ValueView &view, const std::string &name) {
    size_t index     = 0;
    const auto *end  = name.data() + name.size();
    const auto parse = std::from_chars(name.data(), end, index);
    const bool isIndex = !name.empty() && parse.ec == std::errc() && parse.ptr == end;
    // All of them: an index may be past the first screenful of an array.
    size_t total = 0;
    auto all     = members(view, isIndex ? index + 1 : static_cast<size_t>(-1), &total);
    if (isIndex) {
        if (index < all.size()) {
            return all[index].value;
        }
        return std::nullopt;
    }
    for (const auto &m : all) {
        if (m.name == name) {
            return m.value;
        }
    }
    return std::nullopt;
}

} // namespace debugger::core
