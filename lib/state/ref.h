// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The box a `Ref<T>` handle points at.
 *
 * The type itself is the core's (camel/core/type/ref.h): a handle, one resource per value. What
 * this file holds is the storage: one box per reference, holding a value of `T`, and a root that
 * keeps a traced content alive.
 *
 * The box is counted, and the count is the number of handles: a value copy of a handle retains the
 * box (ForeignResourceObject::clone), and the wrapper's finalizer releases it when the collector
 * finds that handle dead. The last release frees the box. There is no `drop` and no other way to
 * free one: the program creates a box and passes its handle, and the box goes when nobody holds it.
 *
 * The content may be anything but a type that holds a `Ref` (the front end refuses one: two boxes
 * holding each other's handles would never reach a count of zero).
 */

#pragma once

#include "camel/core/mm.h"
#include "camel/core/mm/root_handle.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/type/ref.h"

#include <memory>

namespace camel::statemod {

namespace mm     = camel::core::mm;
namespace rtdata = camel::core::rtdata;
namespace type   = camel::core::type;

/// One box: the content and its type, and a root that keeps a traced content alive and follows it
/// when the collector moves it. Its lifetime is the number of handles that point at it.
///
/// The root is not an optimization. The box is a C++ object: the collector walks the handles, the
/// closures and the arrays of the program, and it does not walk a box. A value written into a box
/// after the collection that promoted the box's handle would therefore be invisible to the next
/// minor collection -- a young object held by nothing the collector can see -- and reading it back
/// later would read freed memory. With the root, the content is reachable from outside the
/// collected spaces, and a relocation rewrites the root.
class RefBox final {
  public:
    RefBox(type::Type *contentType, slot_t value);
    ~RefBox();

    RefBox(const RefBox &)            = delete;
    RefBox &operator=(const RefBox &) = delete;

    type::Type *contentType() const { return contentType_; }
    /// The content as a value of `contentType()`: read the root each time, never a cached pointer.
    slot_t get() const;
    /// Writes the content. The new value comes from `ref`/`set`/`update`, where the compiler has
    /// already held it to the box's type, so the type is not changed here.
    void set(slot_t value);

  private:
    type::Type *contentType_ = nullptr;
    /// The content when the collector does not trace it (an `int`, a `float`, ...).
    slot_t inline_ = NullSlot;
    /// The content when it does: the root is the one place the box names it, so a value written
    /// after a collection is kept alive and relocating the object rewrites the root.
    std::unique_ptr<mm::RootHandle> root_;
};

} // namespace camel::statemod