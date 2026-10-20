/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Camel Contributors
 * Created: Oct. 20, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/array/block_selftest.h"

#include "camel/core/array/dlpack.h"
#include "camel/core/mm.h"
#include "camel/core/mm/block.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/type/composite/array.h"

#include <span>
#include <stdexcept>

namespace camel::core::array {

bool debugRunBlockSelfTest() {
    using camel::core::type::ArrayType;
    using camel::core::type::Type;

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

    // The arrays stay alive for the rest of the process (they are GC objects); the pass is a
    // diagnostic, not a unit test with teardown.
    return true;
}

} // namespace camel::core::array