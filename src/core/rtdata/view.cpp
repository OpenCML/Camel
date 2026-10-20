/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Camel Contributors
 * Created: Oct. 21, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/rtdata/view.h"

#include "camel/core/mm.h"
#include "camel/core/rtdata/complex.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/pure.h"
#include "camel/utils/assert.h"

#include <array>
#include <cstring>
#include <format>

namespace camel::core::rtdata {

namespace {

// A view object is a Struct/Tuple header whose ViewData sits immediately before it, so the
// object itself keeps the normal header size and no field is added to every Struct/Tuple. The
// ring is thread-local and allocation-free: a view is only valid until it is overwritten, and
// the store paths materialise one that would outlive the read.
template <typename T, typename Data> struct ViewCell {
    static constexpr size_t kObjectOffset =
        (sizeof(Data) + alignof(T) - 1) / alignof(T) * alignof(T);
    alignas(T) std::byte storage[kObjectOffset + sizeof(T)];
};

template <typename T, typename Data, size_t N> struct ViewRing {
    std::array<ViewCell<T, Data>, N> cells;
    size_t next = 0;

    ViewCell<T, Data> &acquire() {
        ViewCell<T, Data> &cell = cells[next % N];
        ++next;
        return cell;
    }
};

constexpr size_t kViewRingSize = 128;

ViewRing<Struct, Struct::ViewData, kViewRingSize> &structViews() {
    static thread_local ViewRing<Struct, Struct::ViewData, kViewRingSize> ring;
    return ring;
}

ViewRing<Tuple, Tuple::ViewData, kViewRingSize> &tupleViews() {
    static thread_local ViewRing<Tuple, Tuple::ViewData, kViewRingSize> ring;
    return ring;
}

} // namespace

Struct *newStructView(const Struct::ViewData &data) {
    ViewCell<Struct, Struct::ViewData> &cell = structViews().acquire();
    new (cell.storage + ViewCell<Struct, Struct::ViewData>::kObjectOffset -
         sizeof(Struct::ViewData)) Struct::ViewData(data);
    return new (cell.storage + ViewCell<Struct, Struct::ViewData>::kObjectOffset)
        Struct(data.layout->fieldCount, Struct::kViewFlag);
}

Tuple *newTupleView(const Tuple::ViewData &data) {
    ViewCell<Tuple, Tuple::ViewData> &cell = tupleViews().acquire();
    new (cell.storage + ViewCell<Tuple, Tuple::ViewData>::kObjectOffset -
         sizeof(Tuple::ViewData)) Tuple::ViewData(data);
    return new (cell.storage + ViewCell<Tuple, Tuple::ViewData>::kObjectOffset)
        Tuple(data.layout->fieldCount, Tuple::kViewFlag);
}

// A slot-shaped element is read one field at a time and never nests a view inside itself, so a
// single reusable view per kind is enough for the whole loop: only the bytes pointer changes.
Struct *reusedStructView(const Struct::ViewData &data) {
    static thread_local ViewCell<Struct, Struct::ViewData> cell{};
    new (cell.storage + ViewCell<Struct, Struct::ViewData>::kObjectOffset -
         sizeof(Struct::ViewData)) Struct::ViewData(data);
    return new (cell.storage + ViewCell<Struct, Struct::ViewData>::kObjectOffset)
        Struct(data.layout->fieldCount, Struct::kViewFlag);
}

Tuple *reusedTupleView(const Tuple::ViewData &data) {
    static thread_local ViewCell<Tuple, Tuple::ViewData> cell{};
    new (cell.storage + ViewCell<Tuple, Tuple::ViewData>::kObjectOffset -
         sizeof(Tuple::ViewData)) Tuple::ViewData(data);
    return new (cell.storage + ViewCell<Tuple, Tuple::ViewData>::kObjectOffset)
        Tuple(data.layout->fieldCount, Tuple::kViewFlag);
}
slot_t pureViewOf(
    const std::byte *bytes, const type::Type *type, const type::DataLayout *layout,
    size_t stride) {
    ASSERT(
        bytes != nullptr && type != nullptr && layout != nullptr && layout->aggregate,
        "A view is a pure-data aggregate");
    if (type->code() == type::TypeCode::Struct) {
        return toSlot(
            layout->slotShaped ? reusedStructView({bytes, layout, type, stride})
                               : newStructView({bytes, layout, type, stride}));
    }
    ASSERT(type->code() == type::TypeCode::Tuple, "A pure-data aggregate is a struct or tuple");
    return toSlot(
        layout->slotShaped ? reusedTupleView({bytes, layout, type, stride})
                           : newTupleView({bytes, layout, type, stride}));
}


slot_t materializeIfView(slot_t value, const type::Type *type) {
    if (value == NullSlot || type == nullptr) {
        return value;
    }
    if (type->code() == type::TypeCode::Struct) {
        Struct *object = fromSlot<Struct *>(value);
        if (object == nullptr || !object->isView()) {
            return value;
        }
        return toSlot(materializeAggregate(object->viewData()->bytes, type, mm::autoSpace()));
    }
    if (type->code() == type::TypeCode::Tuple) {
        Tuple *object = fromSlot<Tuple *>(value);
        if (object == nullptr || !object->isView()) {
            return value;
        }
        return toSlot(materializeAggregate(object->viewData()->bytes, type, mm::autoSpace()));
    }
    return value;
}

slot_t readPureComplex(const std::byte *bytes, type::TypeCode code) {
    if (code == type::TypeCode::Complex64) {
        float real = 0.0f, imaginary = 0.0f;
        std::memcpy(&real, bytes, sizeof(real));
        std::memcpy(&imaginary, bytes + sizeof(real), sizeof(imaginary));
        return toSlot(Complex::create(real, imaginary, code, mm::autoSpace()));
    }
    ASSERT(code == type::TypeCode::Complex128, "A complex code is c64 or c128");
    double real = 0.0, imaginary = 0.0;
    std::memcpy(&real, bytes, sizeof(real));
    std::memcpy(&imaginary, bytes + sizeof(real), sizeof(imaginary));
    return toSlot(Complex::create(real, imaginary, code, mm::autoSpace()));
}

bool isViewSlot(slot_t slot, const type::Type *type) {
    if (slot == NullSlot || type == nullptr) {
        return false;
    }
    if (type->code() == type::TypeCode::Struct) {
        const auto *object = fromSlot<const Struct *>(slot);
        return object != nullptr && object->isView();
    }
    if (type->code() == type::TypeCode::Tuple) {
        const auto *object = fromSlot<const Tuple *>(slot);
        return object != nullptr && object->isView();
    }
    return false;
}

void assertStoredValueIsNotView(slot_t slot, const type::Type *type, std::string_view where) {
#ifndef NDEBUG
    if (isViewSlot(slot, type)) {
        ASSERT(
            false,
            std::format(
                "A compact element view escaped into {} ({}). A view is tied to the thread-local "
                "view ring and must be materialised before it is stored.",
                where,
                type != nullptr ? type->toString() : "unknown"));
    }
#else
    (void)slot;
    (void)type;
    (void)where;
#endif
}

} // namespace camel::core::rtdata