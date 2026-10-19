/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 18, 2026
 * Updated: Oct. 18, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Binary values preserve embedded NULs; text decoding rejects malformed Unicode.
#include "camel/core/rtdata/bytes.h"
#include "camel/core/error/runtime.h"
#include "camel/core/rtdata/string.h"
#include "camel/utils/utf8.h"
#include <string>
#include <cstring>
#include <limits>
#include <ostream>
namespace camel::core::rtdata {
BytesObject::BytesObject(size_t length) : length_(length) {}
BytesObject *BytesObject::from(std::string_view bytes, mm::IAllocator &allocator) {
    if (bytes.size() > std::numeric_limits<size_t>::max() - sizeof(BytesObject))
        throw std::length_error("Bytes allocation size overflow");
    auto *memory = allocator.alloc(sizeof(BytesObject) + bytes.size(), alignof(BytesObject));
    if (!memory)
        throw std::bad_alloc();
    auto *out = new (memory) BytesObject(bytes.size());
    if (!bytes.empty())
        std::memcpy(out->data_, bytes.data(), bytes.size());
    return out;
}
std::string_view BytesObject::view() const { return {data_, length_}; }
size_t BytesObject::size() const { return length_; }
bool BytesObject::equals(const Object *other, const type::Type *, bool) const {
    return isOfSameCls(this, other) && view() == static_cast<const BytesObject *>(other)->view();
}
Object *BytesObject::clone(mm::IAllocator &allocator, const type::Type *, bool) const {
    // Snapshot before allocating: the source may move at an allocation safepoint.
    const std::string copy(view());
    return from(copy, allocator);
}
void BytesObject::print(std::ostream &os, const type::Type *) const {
    constexpr char hex[] = "0123456789abcdef";
    os << "Bytes[";
    for (size_t i = 0; i < length_; ++i) {
        if (i)
            os << ' ';
        const auto byte = static_cast<unsigned char>(data_[i]);
        os << hex[byte >> 4] << hex[byte & 15];
    }
    os << ']';
}
void BytesObject::onMoved() {}
void BytesObject::updateRefs(const RefRelocator &, const type::Type *) {}
std::optional<size_t> invalidUtf8Offset(std::string_view bytes) {
    const size_t at = camel::utf8::invalidOffset(bytes);
    if (at == camel::utf8::npos)
        return std::nullopt;
    return at;
}
String *checkedString(std::string_view text, mm::IAllocator &allocator, std::string_view source) {
    if (const size_t at = camel::utf8::invalidOffset(text); at != camel::utf8::npos) {
        throwRuntimeFault(
            camel::core::error::RuntimeDiag::InvalidArgument,
            std::string(source),
            "a string must be valid UTF-8, but byte " + std::to_string(at) +
                " is not (use Bytes for binary data)");
    }
    return String::from(std::string(text), allocator);
}
} // namespace camel::core::rtdata
