// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * @file checkreport.cpp
 * @brief The owned side of `CheckReport`: allocation, copying, and the fixed rendering of the
 * table (checkreport.h holds what a row is and why).
 */

#include "checkreport.h"

#include "camel/core/mm.h"

#include <new>
#include <ostream>

namespace camel::rewritemod {

std::string CheckRow::render() const {
    std::string line = verdict + " " + qualified() + ": proposition " + proposition;
    line += ", evidence " + evidence;
    if (!reason.empty()) {
        line += ", reason " + reason;
    }
    if (!detail.empty()) {
        line += ", " + detail;
    }
    if (!coverage.empty()) {
        line += ", coverage " + coverage;
    }
    line += "\n";
    return line;
}

std::string CheckReportObject::text() const {
    std::string text;
    for (const CheckRow &row : rows_) {
        text += row.render();
    }
    return text;
}

CheckReportObject *CheckReportObject::create(std::vector<CheckRow> rows, mm::IAllocator &allocator) {
    void *mem = allocator.alloc(sizeof(CheckReportObject), alignof(CheckReportObject));
    if (mem == nullptr) {
        throw std::bad_alloc();
    }
    auto *report = new (mem) CheckReportObject(std::move(rows));
    // The collector does not look at dead young objects one by one; it must be told which ones own
    // memory it has to release (the same contract as `ProofObject` and `Array`).
    allocator.noteFinalizable(report);
    return report;
}

bool CheckReportObject::equals(const rtdata::Object *other, const Type *, bool) const {
    return this == other;
}

rtdata::Object *CheckReportObject::clone(mm::IAllocator &allocator, const Type *, bool) const {
    return create(rows_, allocator);
}

void CheckReportObject::print(std::ostream &os, const Type *) const { os << text(); }

void CheckReportObject::updateRefs(const rtdata::Object::RefRelocator &, const Type *) {
    // A report is text: it holds no reference into any GC space, so there is nothing to relocate.
}

void CheckReportObject::finalize() noexcept { rows_.clear(); }

} // namespace camel::rewritemod