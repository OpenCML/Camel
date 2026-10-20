/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 21, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/rtdata/view_selftest.h"

#include "camel/core/mm.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/view.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/pure.h"

#include <format>

namespace camel::core::rtdata {

std::string debugRunViewSelfTest() {
    using namespace camel::core::type;

    // A compact aggregate: two fields, not one slot, so reading one element of an array of it
    // goes through the view path.
    StructTypeFactory factory;
    factory.add("f", Type::Float64());
    factory.add("i", Type::Int32());
    StructType *structType = factory.build();
    ArrayType *arrayType   = ArrayType::create(structType);

    Array *array = Array::create(camel::core::mm::autoSpace(), arrayType, 1, true);
    array->ensureBlock();

    // The read a consumer gets without materialising: a view into the array's block.
    const slot_t view = array->getElementView(0);
    const bool viewDetected = isViewSlot(view, structType);

    // The normal path: a store path materialises the view first, and what is stored is a real
    // Struct.
    const slot_t value = materializeIfView(view, structType);
    const bool normalClean = !isViewSlot(value, structType);

    // The escape the debug guard catches: an unmaterialised view still reads as one.
    const bool escapeCaught = viewDetected && view != value;

    return std::format(
        "VIEW_SELFTEST ok normal={} escape={}",
        normalClean ? "clean" : "failed",
        escapeCaught ? "caught" : "missed");
}

} // namespace camel::core::rtdata