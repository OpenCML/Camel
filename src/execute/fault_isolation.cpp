// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/execute/fault_isolation.h"

#include "camel/core/rtdata/tagged.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/utils/type.h"

#include <format>
#include <string>

namespace camel::execute {

using namespace camel::core::type;

std::optional<Type *> resolveFaultIsolationType(const type_vec_t &with, const type_vec_t &norm) {
    if (!with.empty() || norm.empty() || norm[0]->code() != TypeCode::Function) {
        return std::nullopt;
    }
    const auto *funcType = tt::as_ptr<FunctionType>(norm[0]);
    // A function with `with` parameters is called through a closure that binds them.
    if (funcType->withTypesCount() != 0 || funcType->normTypesCount() != norm.size() - 1 ||
        !funcType->hasExitType()) {
        return std::nullopt;
    }
    for (size_t i = 0; i < funcType->normTypesCount(); ++i) {
        if (!funcType->normTypeAt(i)->equals(norm[i + 1])) {
            return std::nullopt;
        }
    }
    Type *exitType = funcType->exitType();
    if (exitType->code() == TypeCode::Tuple && tt::as_ptr<TupleType>(exitType)->isFallible()) {
        return exitType; // `U!` stays `U!`: there is no `U!!`
    }
    return TupleType::fallible(exitType);
}

slot_t wrapIsolatedResult(slot_t value, const Type *calleeResultType, const Type *nodeType) {
    ASSERT(nodeType != nullptr && nodeType->code() == TypeCode::Tuple, "Not a fallible node.");
    const auto *fallibleType = tt::as_ptr<TupleType>(const_cast<Type *>(nodeType));
    ASSERT(fallibleType->isFallible(), "The fault-isolation node must have a `T!` type.");
    if (calleeResultType != nullptr && calleeResultType->code() == TypeCode::Tuple &&
        tt::as_ptr<TupleType>(const_cast<Type *>(calleeResultType))->isFallible()) {
        return value;
    }
    return core::rtdata::makeOk(value, fallibleType);
}

slot_t isolatedFaultToError(const core::error::Diagnostic &fault) {
    std::string_view kind = fault.name;
    if (const size_t colons = kind.rfind("::"); colons != std::string_view::npos) {
        kind.remove_prefix(colons + 2);
    }
    std::string location;
    if (const auto range = core::error::resolveSourceRange(fault.range, fault.sourceContext)) {
        // The file name only: an error value must not differ with where the program is stored.
        std::string path   = core::error::diagnosticPath(fault);
        const size_t slash = path.find_last_of("/\\");
        if (slash != std::string::npos) {
            path.erase(0, slash + 1);
        }
        location = std::format("{}:{}:{}", path, range->start.line + 1, range->start.character + 1);
    }
    return core::rtdata::makeError(kind, fault.message, location);
}

} // namespace camel::execute
