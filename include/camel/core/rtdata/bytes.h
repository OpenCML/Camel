/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 18, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Bytes: binary data (design principles 11.1; data block design 10.5).
 *
 * Small values (under mm::kInlineBlockBytes) keep their bytes inline in the GC object and move
 * with it. Larger values live in a data block, which does not move, so a file read can fill it
 * directly and Python or a native kernel can read it without another copy. An adopted block
 * (Python bytes, a mapped file later) runs its owner's release callback when the value dies.
 */
#pragma once
#include "camel/core/mm/block.h"
#include "camel/core/rtdata/base.h"
#include <optional>
#include <string_view>
class String;
namespace camel::core::rtdata {
class BytesObject final : public Object {
  public:
    static BytesObject *from(std::string_view bytes, mm::IAllocator &allocator);
    /// A Bytes value over `block`'s bytes. Ownership of the caller's reference transfers to
    /// the value; the block is released from the GC finalizer. `length` may be shorter than
    /// the block (a prefix read).
    static BytesObject *adopt(mm::Block *block, size_t length, mm::IAllocator &allocator);
    std::string_view view() const;
    size_t size() const;
    bool equals(const Object *other, const type::Type *type, bool deep = false) const override;
    Object *
    clone(mm::IAllocator &allocator, const type::Type *type, bool deep = false) const override;
    void print(std::ostream &os, const type::Type *type) const override;
    void onMoved() override;
    void updateRefs(const RefRelocator &, const type::Type *) override;
    void finalize() noexcept override;

  private:
    explicit BytesObject(size_t length, mm::Block *block);
    size_t length_;
    /// Null when the bytes are inline (`data_`); otherwise the data block.
    mm::Block *block_ = nullptr;
    char data_[];
};
// Returns the first malformed byte; a missing value means valid UTF-8.
std::optional<size_t> invalidUtf8Offset(std::string_view bytes);
// A string from text that came from outside (a stream, the environment, a command line): a
// `string` is always valid UTF-8 (design principles 11.1), so malformed text is a fault here,
// named after `source`.
String *checkedString(std::string_view text, mm::IAllocator &allocator, std::string_view source);
} // namespace camel::core::rtdata