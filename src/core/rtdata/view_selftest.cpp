// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/rtdata/view_selftest.h"

#include "camel/core/mm.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/rtdata/view.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/pure.h"

#include <format>
#include <string>
#include <vector>

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

    // The escape that actually happened, through a call boundary into reduce's accumulator. A
    // slot-shaped element -- every field an 8-byte leaf -- is read through one reused
    // thread-local cell, so reading the next element overwrites the first view in place. An
    // accumulator that kept that view therefore names the element being read now. The call
    // boundary materialises it (materializeCallResult), and the kept value no longer moves.
    TupleType *pairType = TupleType::create(std::vector<Type *>{Type::Int64(), Type::Int64()});
    ArrayType *pairsType = ArrayType::create(pairType);
    Array *pairs         = Array::create(camel::core::mm::autoSpace(), pairsType, 3, true);
    const int64_t elements[3][2] = {{1, 30}, {2, 6}, {3, 20}};
    for (size_t i = 0; i < 3; ++i) {
        Tuple *element = Tuple::create(2, camel::core::mm::autoSpace());
        element->set<int64_t>(0, elements[i][0], pairType);
        element->set<int64_t>(1, elements[i][1], pairType);
        pairs->set<slot_t>(i, toSlot(element), pairsType);
    }
    pairs->ensureBlock();

    const slot_t first = pairs->getElementView(0);
    // What reduce keeps as its accumulator: the value the element callback returned, through the
    // same boundary the operators use.
    const slot_t kept  = materializeCallResult(first, pairType);
    const slot_t next  = pairs->getElementView(1);

    // The reused cell: one view object serves every element of this type.
    const bool aliased   = first == next;
    const bool moved     = fromSlot<const Tuple *>(first)->get<int64_t>(0) == 2;
    const bool keptClean = !isViewSlot(kept, pairType);
    const bool keptStable =
        fromSlot<const Tuple *>(kept)->get<int64_t>(0) == 1 &&
        fromSlot<const Tuple *>(kept)->get<int64_t>(1) == 30;

    return std::format(
        "VIEW_SELFTEST ok normal={} escape={}\n"
        "VIEW_ACCUMULATOR ok alias={} kept={}",
        normalClean ? "clean" : "failed",
        escapeCaught ? "caught" : "missed",
        aliased && moved ? "detected" : "missed",
        keptClean && keptStable ? "stable" : "moved");
}

std::string debugRunViewEscapeSelfTest() {
    using namespace camel::core::type;

    // The escape re-created on purpose: a slot-shaped element is read through the reused
    // thread-local view cell, a callback would return it, and the reduce would keep it as its
    // accumulator. The store below is the production boundary (`storeReduceAccumulator`), the
    // same one the nvm reduce operator calls; in a debug build its guard aborts here.
    TupleType *pairType  = TupleType::create(std::vector<Type *>{Type::Int64(), Type::Int64()});
    ArrayType *pairsType = ArrayType::create(pairType);
    Array *pairs         = Array::create(camel::core::mm::autoSpace(), pairsType, 3, true);
    const int64_t elements[3][2] = {{1, 30}, {2, 6}, {3, 20}};
    for (size_t i = 0; i < 3; ++i) {
        Tuple *element = Tuple::create(2, camel::core::mm::autoSpace());
        element->set<int64_t>(0, elements[i][0], pairType);
        element->set<int64_t>(1, elements[i][1], pairType);
        pairs->set<slot_t>(i, toSlot(element), pairsType);
    }
    pairs->ensureBlock();

    const slot_t view = pairs->getElementView(0);

    ArrayType *cellType = ArrayType::create(pairType);
    Array *cell         = Array::create(camel::core::mm::autoSpace(), cellType, 1);
    storeReduceAccumulator(cell, view, cellType);

    // Unreachable in a debug build. A release build has no guard, so the store materialised the
    // view; saying so is what turns a missing abort into a loud test failure.
    return "VIEW_ESCAPE not-caught: the store guard is compiled out of this build";
}

} // namespace camel::core::rtdata
