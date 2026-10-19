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
    size_t i = 0;
    while (i < bytes.size()) {
        const size_t start = i;
        const auto first   = static_cast<unsigned char>(bytes[i++]);
        if (first < 0x80)
            continue;
        size_t count;
        uint32_t value, least;
        if (first >= 0xc2 && first <= 0xdf)
            count = 1, value = first & 31, least = 0x80;
        else if (first >= 0xe0 && first <= 0xef)
            count = 2, value = first & 15, least = 0x800;
        else if (first >= 0xf0 && first <= 0xf4)
            count = 3, value = first & 7, least = 0x10000;
        else
            return start;
        if (count > bytes.size() - i)
            return start;
        for (size_t j = 0; j < count; ++j) {
            const auto next = static_cast<unsigned char>(bytes[i++]);
            if ((next & 0xc0) != 0x80)
                return start;
            value = (value << 6) | (next & 63);
        }
        if (value < least || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
            return start;
    }
    return std::nullopt;
}
} // namespace camel::core::rtdata
