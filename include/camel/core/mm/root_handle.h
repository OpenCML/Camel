// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/mm/gen.h"

#include <string>

// RAII root for C++ code that holds an auto-space object across a safepoint.
//
// While a RootHandle exists the object it names is alive, and after every safepoint get()
// returns where the object is now: the collector rewrites the handle when it moves the object.
// The pointer get() returns is an ordinary raw pointer again and is good until the next
// safepoint only; read it from the handle each time it is needed after one.
//
// Cost: creating and destroying one takes the allocator's lock once each and links the handle
// into a list the collector walks; no allocation unless the name is longer than the small
// string buffer. That is fine per call of an operator or per rewrite step, not per element of
// a loop: root the container, not its elements.
//
// A handle is neither copyable nor movable (the collector holds its address). To keep a set
// of objects, use a std::deque<RootHandle> (emplace_back does not move the others) or
// std::unique_ptr<RootHandle>. A handle may live as long as the process; one that outlives the
// allocator is a programming error. Handles may be created and destroyed on any thread, but
// not from inside a collection (a finalizer or a trace callback).

namespace camel::core::mm {

class RootHandle final {
  public:
    explicit RootHandle(GenerationalAllocatorWithGC &allocator, std::string name = "RootHandle");
    RootHandle(
        GenerationalAllocatorWithGC &allocator, rtdata::Object *object, const type::Type *type,
        std::string name = "RootHandle");
    ~RootHandle();

    RootHandle(const RootHandle &)            = delete;
    RootHandle &operator=(const RootHandle &) = delete;
    RootHandle(RootHandle &&)                 = delete;
    RootHandle &operator=(RootHandle &&)      = delete;

    rtdata::Object *get() const noexcept { return object_; }

    template <typename T> T *getAs() const noexcept { return static_cast<T *>(object_); }

    const type::Type *type() const noexcept { return type_; }

    const std::string &name() const noexcept { return name_; }

    void reset(rtdata::Object *object, const type::Type *type);

  private:
    friend class GenerationalAllocatorWithGC;

    void trace(const GenerationalAllocatorWithGC::RefRelocator &relocate);

    // Links of the allocator's list of handles (guarded by its lock).
    RootHandle *prev_ = nullptr;
    RootHandle *next_ = nullptr;

    GenerationalAllocatorWithGC *allocator_ = nullptr;
    rtdata::Object *object_                 = nullptr;
    const type::Type *type_                 = nullptr;
    std::string name_;
};

} // namespace camel::core::mm
