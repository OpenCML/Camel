/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 18, 2026
 * Updated: Oct. 18, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Inline, immutable bytes move with the GC object and contain no traced references.
#pragma once
#include "camel/core/rtdata/base.h"
#include <optional>
#include <string_view>
class String;
namespace camel::core::rtdata {
class BytesObject final : public Object {
  public:
    static BytesObject *from(std::string_view bytes, mm::IAllocator &allocator);
    std::string_view view() const;
    size_t size() const;
    bool equals(const Object *other, const type::Type *type, bool deep = false) const override;
    Object *
    clone(mm::IAllocator &allocator, const type::Type *type, bool deep = false) const override;
    void print(std::ostream &os, const type::Type *type) const override;
    void onMoved() override;
    void updateRefs(const RefRelocator &, const type::Type *) override;

  private:
    explicit BytesObject(size_t length);
    size_t length_;
    char data_[];
};
// Returns the first malformed byte; a missing value means valid UTF-8.
std::optional<size_t> invalidUtf8Offset(std::string_view bytes);
// A string from text that came from outside (a stream, the environment, a command line): a
// `string` is always valid UTF-8 (design principles 11.1), so malformed text is a fault here,
// named after `source`.
String *checkedString(std::string_view text, mm::IAllocator &allocator, std::string_view source);
} // namespace camel::core::rtdata
