// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Copy payload bits rather than converting through floating-point arithmetic.

#include "binary.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/pure.h"
#include "camel/core/rtdata/array.h"
#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>

namespace camel::tensor {
namespace {
void convertEndian(std::string &bytes, size_t width) {
    static_assert(
        std::endian::native == std::endian::little || std::endian::native == std::endian::big);
    if constexpr (std::endian::native == std::endian::big) {
        for (size_t offset = 0; offset < bytes.size(); offset += width) {
            std::reverse(bytes.begin() + offset, bytes.begin() + offset + width);
        }
    }
}

size_t checkedBytes(std::span<const int64_t> shape, size_t width) {
    if (shape.size() > std::numeric_limits<uint32_t>::max()) {
        throw std::invalid_argument("tensor rank exceeds the storage limit");
    }
    bool empty = false;
    for (const auto extent : shape) {
        if (extent < 0) {
            throw ShapeError("binary tensor shape cannot contain negative extents");
        }
        empty |= extent == 0;
    }
    if (empty) {
        return 0;
    }
    size_t size = width;
    for (const auto extent : shape) {
        if (static_cast<uint64_t>(extent) > std::numeric_limits<size_t>::max() / size) {
            throw ShapeError("binary tensor byte count overflows the address space");
        }
        size *= static_cast<size_t>(extent);
    }
    return size;
}
} // namespace

size_t tensorPayloadSize(type::TypeCode dtype, std::span<const int64_t> shape) {
    return checkedBytes(shape, elementSize(dtype));
}

std::string tensorToLittleEndian(const Array *tensor) {
    std::string bytes;
    if (tensor->byteSize()) {
        bytes.assign(reinterpret_cast<const char *>(tensor->rawData()), tensor->byteSize());
    }
    convertEndian(bytes, elementSize(tensor->dtype()));
    return bytes;
}

Array *tensorFromLittleEndian(
    std::string_view input, type::TypeCode dtype, std::span<const int64_t> shape,
    mm::IAllocator &allocator) {
    const size_t width    = elementSize(dtype);
    const size_t expected = tensorPayloadSize(dtype, shape);
    if (input.size() != expected) {
        throw ShapeError(
            "binary tensor payload has " + std::to_string(input.size()) +
            " bytes; shape and dtype require " + std::to_string(expected));
    }
    if (dtype == type::TypeCode::Bool) {
        for (size_t i = 0; i < input.size(); ++i) {
            if (input[i] != 0 && input[i] != 1) {
                throw std::invalid_argument(
                    "binary bool payload must contain only 0 or 1 at byte " + std::to_string(i));
            }
        }
    }
    // Input may belong to the moving Camel heap: take ownership before allocation.
    std::string bytes(input);
    convertEndian(bytes, width);
    const Shape ownedShape(shape.begin(), shape.end());
    Array *result = createTensor(dtype, ownedShape, allocator);
    if (!bytes.empty()) {
        std::memcpy(result->rawData(), bytes.data(), bytes.size());
    }
    return result;
}

namespace {
/// The field's type, by the field list of a struct or a tuple.
const type::Type *fieldTypeOf(const type::Type *aggregate, size_t field) {
    if (aggregate->code() == type::TypeCode::Struct) {
        return static_cast<const type::StructType *>(aggregate)->typeAt(field);
    }
    return static_cast<const type::TupleType *>(aggregate)->typeAt(field);
}

/// One leaf's bytes as little-endian bytes of `width`: a copy on a little-endian host, a copy of
/// the reversed bytes otherwise.
void leafToLittleEndian(std::byte *out, const std::byte *leaf, size_t width) {
    std::memcpy(out, leaf, width);
    if constexpr (std::endian::native == std::endian::big) {
        std::reverse(out, out + width);
    }
}

/// The read side of leafToLittleEndian: the leaf at `out`, from little-endian bytes.
void leafFromLittleEndian(std::byte *out, const std::byte *leaf, size_t width) {
    leafToLittleEndian(out, leaf, width);
}

/// One element of `elementType`, little-endian. A leaf is the element itself; an aggregate is its
/// fields one after another, each at the offset its layout gives it and each of the width its own
/// code has. The padding between and after the fields is not a field, so nothing here writes it:
/// the payload's padding is whatever the element held there.
void elementToLittleEndian(
    std::byte *out, const std::byte *element, const type::Type *elementType,
    const type::DataLayout *layout) {
    if (!layout->aggregate) {
        leafToLittleEndian(out, element, elementSize(layout->scalar));
        return;
    }
    for (size_t field = 0; field < layout->fieldCount; ++field) {
        const type::Type *fieldType         = fieldTypeOf(elementType, field);
        const type::DataLayout *fieldLayout = type::pureDataLayout(fieldType);
        ASSERT(fieldLayout != nullptr, "A pure-data field has a layout");
        const size_t offset = layout->fieldOffsets[field];
        elementToLittleEndian(out + offset, element + offset, fieldType, fieldLayout);
    }
}

/// The read side of elementToLittleEndian.
void elementFromLittleEndian(
    std::byte *element, const std::byte *in, const type::Type *elementType,
    const type::DataLayout *layout) {
    if (!layout->aggregate) {
        leafFromLittleEndian(element, in, elementSize(layout->scalar));
        return;
    }
    for (size_t field = 0; field < layout->fieldCount; ++field) {
        const type::Type *fieldType         = fieldTypeOf(elementType, field);
        const type::DataLayout *fieldLayout = type::pureDataLayout(fieldType);
        ASSERT(fieldLayout != nullptr, "A pure-data field has a layout");
        const size_t offset = layout->fieldOffsets[field];
        elementFromLittleEndian(element + offset, in + offset, fieldType, fieldLayout);
    }
}
} // namespace

std::string plainSequenceToLittleEndian(const Array *array) {
    ASSERT(array != nullptr, "a plain sequence needs a value");
    const type::Type *elementType = array->elemType();
    ASSERT(elementType != nullptr, "a plain sequence states its element type");
    const type::DataLayout *layout = type::pureDataLayout(elementType);
    ASSERT(layout != nullptr, "a plain sequence holds pure data");
    ASSERT(
        array->isCompact() && !array->isView(),
        "a plain sequence of an aggregate element type has one compact run of bytes");

    const size_t width = array->elementSize();
    std::string bytes(static_cast<size_t>(array->numel()) * width, '\0');
    for (uint64_t element = 0; element < array->numel(); ++element) {
        elementToLittleEndian(
            reinterpret_cast<std::byte *>(bytes.data()) + element * width,
            array->elementPtr(element),
            elementType,
            layout);
    }
    return bytes;
}

Array *plainSequenceFromLittleEndian(
    std::string_view input, const type::ArrayType *arrayType, std::span<const int64_t> extents,
    mm::IAllocator &allocator) {
    const type::Type *elementType   = arrayType->elemType();
    const type::DataLayout *layout  = type::pureDataLayout(elementType);
    if (layout == nullptr || layout->size == 0) {
        throw std::invalid_argument("the element type of a binary sequence must be pure data");
    }
    // The reader walks the element type's layout, so an aggregate is what it can fill.
    if (!layout->aggregate) {
        throw std::invalid_argument(
            "the element type of a scalar binary sequence is a storage dtype");
    }
    const uint64_t count = numelOf(extents);
    if (static_cast<uint64_t>(input.size()) != count * layout->size) {
        throw ShapeError(
            "binary tensor payload has " + std::to_string(input.size()) +
            " bytes; shape and dtype require " + std::to_string(count * layout->size));
    }
    Array *result = Array::create(allocator, arrayType, extents, true);
    for (uint64_t element = 0; element < count; ++element) {
        elementFromLittleEndian(
            result->elementPtr(element),
            reinterpret_cast<const std::byte *>(input.data()) + element * layout->size,
            elementType,
            layout);
    }
    return result;
}
} // namespace camel::tensor
