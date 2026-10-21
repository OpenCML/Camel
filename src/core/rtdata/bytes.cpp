/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 18, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Binary values preserve embedded NULs; text decoding rejects malformed Unicode.
#include "camel/core/rtdata/bytes.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/string.h"
#include "camel/utils/utf8.h"
#include <cstring>
#include <limits>
#include <ostream>
#include <string>
namespace camel::core::rtdata {
BytesObject::BytesObject(size_t length, mm::Block *block) : length_(length), block_(block) {}
BytesObject *BytesObject::from(std::string_view bytes, mm::IAllocator &allocator) {
    // Snapshot first: the source may be a GC value, and both the allocation below and the
    // block creation can move it at a safepoint.
    const std::string copy(bytes);
    if (copy.size() < mm::kInlineBlockBytes) {
        if (copy.size() > std::numeric_limits<size_t>::max() - sizeof(BytesObject))
            throw std::length_error("Bytes allocation size overflow");
        auto *memory =
            allocator.alloc(sizeof(BytesObject) + copy.size(), alignof(BytesObject));
        if (!memory)
            throw std::bad_alloc();
        auto *out = new (memory) BytesObject(copy.size(), nullptr);
        if (!copy.empty())
            std::memcpy(out->data_, copy.data(), copy.size());
        return out;
    }

    mm::Block *block = mm::Block::create(copy.size());
    if (!copy.empty())
        std::memcpy(block->data(), copy.data(), copy.size());
    return adopt(block, copy.size(), allocator);
}
BytesObject *BytesObject::adopt(mm::Block *block, size_t length, mm::IAllocator &allocator) {
    auto *memory = allocator.alloc(sizeof(BytesObject), alignof(BytesObject));
    if (!memory) {
        block->release();
        throw std::bad_alloc();
    }
    auto *out = new (memory) BytesObject(length, block);
    allocator.noteFinalizable(out);
    return out;
}
std::string_view BytesObject::view() const {
    return {block_ != nullptr ? reinterpret_cast<const char *>(block_->data()) : data_, length_};
}
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
    const std::string_view bytes = view();
    os << "Bytes[";
    for (size_t i = 0; i < length_; ++i) {
        if (i)
            os << ' ';
        const auto byte = static_cast<unsigned char>(bytes[i]);
        os << hex[byte >> 4] << hex[byte & 15];
    }
    os << ']';
}
void BytesObject::onMoved() {}
void BytesObject::updateRefs(const RefRelocator &, const type::Type *) {}
void BytesObject::finalize() noexcept {
    if (block_ != nullptr) {
        block_->releaseFromFinalizer();
        block_ = nullptr;
    }
}
size_t BytesObject::blockBytesHeld() const noexcept {
    return block_ != nullptr ? block_->bytes() : 0;
}
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