// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/array/block_selftest.h"

#include "camel/core/array/dlpack.h"
#include "camel/core/mm.h"
#include "camel/core/mm/block.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/complex.h"
#include "camel/core/type/composite/array.h"

#include <cmath>
#include <span>
#include <stdexcept>

namespace camel::core::array {

bool debugRunBlockSelfTest() {
    using camel::core::type::ArrayType;
    using camel::core::type::Type;
    using camel::core::type::TypeCode;

    const size_t bytesBefore       = mm::blockBytesLive();
    const uint64_t creationsBefore = mm::blockCreations();

    // A small numeric array lives inline in its array object: no block, no block bytes.
    Array *small = Array::create(mm::autoSpace(), ArrayType::create(Type::Int64()), 3);
    if (!small->isCompact() || small->block() != nullptr || small->byteSize() != 24) {
        throw std::runtime_error("block self-test: an int[3] is not inline");
    }
    for (int i = 0; i < 3; ++i) {
        small->set<rtdata::Int64>(i, static_cast<rtdata::Int64>(i + 1));
    }
    if (small->get<rtdata::Int64>(2) != 3 || mm::blockBytesLive() != bytesBefore) {
        throw std::runtime_error("block self-test: an inline array counts block bytes");
    }

    // A view promotes the inline array once, and both share one block.
    Array *viewed = Array::view(small, std::span<const int64_t>(small->shape(), 1));
    if (small->block() == nullptr || viewed->block() != small->block() ||
        small->block()->refCount() < 2 || mm::blockCreations() != creationsBefore + 1 ||
        mm::blockBytesLive() != bytesBefore + 24) {
        throw std::runtime_error("block self-test: a view did not share the promoted block");
    }
    if (viewed->get<rtdata::Int64>(2) != 3) {
        throw std::runtime_error("block self-test: a view does not read the source");
    }

    // A large array is block backed from the start.
    Array *big = Array::create(mm::autoSpace(), ArrayType::create(Type::Int64()), 100);
    if (!big->isCompact() || big->block() == nullptr || big->byteSize() != 800) {
        throw std::runtime_error("block self-test: an int[100] is not block backed");
    }
    for (size_t i = 0; i < 100; ++i) {
        big->set<rtdata::Int64>(i, static_cast<rtdata::Int64>(i));
    }

    // DLPack export is zero-copy: same data address, block retained by the managed tensor.
    DLManagedTensor *managed = toDLPack(big);
    if (managed->dl_tensor.data != big->rawData() || managed->dl_tensor.ndim != 1 ||
        managed->dl_tensor.shape[0] != 100 || big->block()->refCount() < 2) {
        throw std::runtime_error("block self-test: DLPack export copied the data");
    }

    // Import adopts the same address (a second block over the producer's memory).
    Array *imported = fromDLPack(managed, mm::autoSpace());
    if (imported->rawData() != big->rawData() || imported->size() != 100 ||
        imported->get<rtdata::Int64>(42) != 42) {
        throw std::runtime_error("block self-test: DLPack import did not share the data");
    }

    // DLPack carries a complex number as its two parts: dtype code 5, 64 bits for c64 and 128
    //  for c128, lanes 1 (item 5). One tensor of NumPy complex128 is a row of
    // (real, imaginary) doubles, in that order; one of complex64 a row of two floats.
    Array *complex128 = Array::create(mm::autoSpace(), ArrayType::create(Type::Complex128()), 20);
    if (!complex128->isCompact() || complex128->block() == nullptr ||
        complex128->byteSize() != 320 || complex128->elementSize() != 16) {
        throw std::runtime_error("block self-test: a c128[20] is not 320 block-backed bytes");
    }
    const double kComplexValues[4][2] = {
        {1.5, -2.5},
        {0.0, 1.0},
        {-3.25, 0.0},
        {-0.0, 0.5},
    };
    auto *parts = reinterpret_cast<double *>(complex128->rawData());
    for (size_t i = 0; i < 20; ++i) {
        parts[2 * i]     = kComplexValues[i % 4][0];
        parts[2 * i + 1] = kComplexValues[i % 4][1];
    }
    DLManagedTensor *complexManaged = toDLPack(complex128);
    if (complexManaged->dl_tensor.dtype.code != 5 || complexManaged->dl_tensor.dtype.bits != 128 ||
        complexManaged->dl_tensor.dtype.lanes != 1 ||
        complexManaged->dl_tensor.data != complex128->rawData() ||
        complex128->block()->refCount() < 2) {
        throw std::runtime_error("block self-test: a c128 export is not DLPack code 5, 128 bits");
    }
    Array *complexImported = fromDLPack(complexManaged, mm::autoSpace());
    if (complexImported->rawData() != complex128->rawData() ||
        complexImported->elemTypeCode() != TypeCode::Complex128 || complexImported->size() != 20) {
        throw std::runtime_error("block self-test: a complex DLPack import did not share the data");
    }
    // Every part reads back, including the sign of a zero (a complex number is two doubles,
    // not one value whose bits a comparison would lose).
    for (size_t i = 0; i < 20; ++i) {
        const auto *value = complexImported->get<const rtdata::Complex *>(i);
        const double *want = kComplexValues[i % 4];
        if (value->real() != want[0] || value->imaginary() != want[1] ||
            std::signbit(value->real()) != std::signbit(want[0]) ||
            std::signbit(value->imaginary()) != std::signbit(want[1])) {
            throw std::runtime_error("block self-test: a complex DLPack import changed a part");
        }
    }

    Array *complex64 = Array::create(mm::autoSpace(), ArrayType::create(Type::Complex64()), 40);
    if (!complex64->isCompact() || complex64->block() == nullptr ||
        complex64->byteSize() != 320 || complex64->elementSize() != 8) {
        throw std::runtime_error("block self-test: a c64[40] is not 320 block-backed bytes");
    }
    const float kComplex64Values[2][2] = {{0.5f, -1.25f}, {-2.0f, 3.5f}};
    auto *floatParts = reinterpret_cast<float *>(complex64->rawData());
    for (size_t i = 0; i < 40; ++i) {
        floatParts[2 * i]     = kComplex64Values[i % 2][0];
        floatParts[2 * i + 1] = kComplex64Values[i % 2][1];
    }
    DLManagedTensor *complex64Managed = toDLPack(complex64);
    if (complex64Managed->dl_tensor.dtype.code != 5 || complex64Managed->dl_tensor.dtype.bits != 64 ||
        complex64Managed->dl_tensor.dtype.lanes != 1 ||
        complex64Managed->dl_tensor.data != complex64->rawData()) {
        throw std::runtime_error("block self-test: a c64 export is not DLPack code 5, 64 bits");
    }
    Array *complex64Imported = fromDLPack(complex64Managed, mm::autoSpace());
    if (complex64Imported->elemTypeCode() != TypeCode::Complex64 ||
        complex64Imported->size() != 40) {
        throw std::runtime_error("block self-test: a c64 DLPack import changed the element type");
    }
    const auto *part = complex64Imported->get<const rtdata::Complex *>(2);
    if (part->realF() != 0.5f || part->imaginaryF() != -1.25f) {
        throw std::runtime_error("block self-test: a c64 DLPack import changed a part");
    }

    // The arrays stay alive for the rest of the process (they are GC objects); the pass is a
    // diagnostic, not a unit test with teardown.
    return true;
}

} // namespace camel::core::array