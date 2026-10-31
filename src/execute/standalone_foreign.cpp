// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * libcamel's half of camel/standalone/foreign.h: the count of a handle to a value the runtime owns,
 * and the text the runtime prints for it.
 *
 * A Foreign owns one mm::RootHandle, and the count is the number of handles naming the same object
 * -- the collector walks the handles itself, so two Foreigns that name one object are two handles,
 * a copy is another handle, and the last release deletes it and hands the object back to the
 * collector. The text is the runtime's own, because only the runtime knows how its objects print.
 * Both are why this half of the type lives here and not in the header: only the runtime can count
 * its own handles and print its own objects (docs/design/cpp-standalone/README.cn.md, section 9).
 */

#include "camel/standalone/foreign.h"

#include "camel/core/mm.h"
#include "camel/core/rtdata/base.h"

#include <sstream>

namespace camel::standalone {

using camel::core::mm::RootHandle;
using camel::core::rtdata::Object;
using camel::core::type::Type;

void *Foreign::retain(void *handle) noexcept {
    auto *root = static_cast<RootHandle *>(handle);
    if (root == nullptr) {
        return nullptr;
    }
    try {
        return new RootHandle(
            camel::core::mm::autoSpace(), root->get(), root->type(), "standalone Foreign");
    } catch (...) {
        // No room for another handle: the copy is the empty one, which releases nothing, so the
        // original keeps the count it had and nothing is released twice.
        return nullptr;
    }
}

void Foreign::release(void *handle) noexcept { delete static_cast<RootHandle *>(handle); }

std::string Foreign::textOf(void *handle) {
    const auto *root = static_cast<const RootHandle *>(handle);
    if (root == nullptr) {
        return "null";
    }
    std::ostringstream os;
    Object *object = root->get();
    if (root->type() == nullptr) {
        // A value whose type the bridge could not name: its own printer is what the runtime has
        // for it (the two-argument printer is driven by a type, which is why the type is wanted).
        if (object != nullptr) {
            object->print(os, nullptr);
        } else {
            os << "null";
        }
        return os.str();
    }
    // The text `println` writes the value with (core/rtdata/print.cpp): the program and the
    // interpreter print one value the same way, byte for byte.
    camel::core::rtdata::printValue(
        os, camel::core::rtdata::toSlot<Object *>(object), const_cast<Type *>(root->type()));
    return os.str();
}

} // namespace camel::standalone