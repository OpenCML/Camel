// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/utils/assert.h"
#include "camel/utils/debug.h"
#include "header.h"

#include <functional>
#include <new>
#include <type_traits>
#include <utility> // for std::forward
#include <vector>

namespace camel::core::rtdata {
class Object;
}

namespace camel::core::mm {

class Block;

constexpr size_t KB = 1024;
constexpr size_t MB = 1024 * KB;
constexpr size_t GB = 1024 * MB;

class IAllocator {
  public:
    virtual ~IAllocator() = default;

    // Allocate memory with optional alignment.
    virtual void *alloc(size_t size, size_t align = alignof(slot_t)) = 0;

    // Free a single object (may be a no-op in a GC nursery).
    virtual void free(void *ptr) = 0;

    // Bulk clear (suitable for semispaces / bump-pointer allocators).
    virtual void reset() { ASSERT(false, "reset() not implemented for this allocator"); };

    // Query remaining available space.
    virtual size_t available() const {
        ASSERT(false, "available() not implemented for this allocator");
        return 0;
    };

    // Check whether the pointer belongs to this allocator's managed region.
    virtual bool contains(void *ptr) const {
        ASSERT(false, "contains() not implemented for this allocator");
        return false;
    };

    // Iterate allocated objects (read-only; do not free directly in the callback).
    virtual void iterateAllocated(const std::function<void(ObjectHeader *)> &visitor) const {
        ASSERT(false, "iterateAllocated() not implemented for this allocator");
    };

    // Tells the allocator that the object at `payload`, which it has just handed out, has a
    // finalizer (rtdata::Object::finalize) that must run when the object dies. A collector
    // that reclaims dead objects without visiting them needs to know; the others do not.
    virtual void noteFinalizable(void *payload) { (void)payload; }

    // Tells the allocator that `owner` now holds `block`: the array layer calls this when an
    // array takes a block over (a view of an inline array, a growth, an in-place operation).
    // A collector that paces the block bytes held by elder values counts the block here, so an
    // array that is already in the elder space when it gets a block counts like any other.
    // Other allocators ignore it.
    virtual void noteBlockHeld(camel::core::rtdata::Object *owner, Block *block) {
        (void)owner;
        (void)block;
    }

    // Bulk-free the given list of object headers.
    virtual void freeBulk(const std::vector<ObjectHeader *> &objects) {
        for (auto *obj : objects) {
            free(reinterpret_cast<void *>(reinterpret_cast<uint8_t *>(obj) + sizeof(ObjectHeader)));
        }
    };
};

template <typename T, typename... Args>
inline T *constructAt(IAllocator &allocator, Args &&...args) {
    // Allocate memory aligned to T.
    void *ptr = allocator.alloc(sizeof(T), alignof(T));
    if (!ptr) {
        throw std::bad_alloc();
    }

    // Construct the object in the allocated memory via placement new.
    return new (ptr) T(std::forward<Args>(args)...);
}

inline size_t alignUp(size_t n, size_t align) { return (n + align - 1) & ~(align - 1); }

template <typename T> inline std::string formatAddress(T *ptr, bool half = false) {
    std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(ptr);

    std::stringstream ss;
    ss << std::hex << std::uppercase << addr;
    std::string hexStr = ss.str();

    if (half) {
        if (hexStr.length() < 8) {
            hexStr = std::string(8 - hexStr.length(), '0') + hexStr;
        } else {
            hexStr = hexStr.substr(hexStr.length() - 8);
        }
    } else {
        if (hexStr.length() < 16) {
            hexStr = std::string(16 - hexStr.length(), '0') + hexStr;
        }
    }

    std::string formatted;
    for (size_t i = 0; i < hexStr.length(); ++i) {
        formatted += hexStr[i];
        if ((i + 1) % 4 == 0 && i + 1 != hexStr.length())
            formatted += '\'';
    }

    return "0x" + formatted;
}

} // namespace camel::core::mm
