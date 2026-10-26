// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/execute/fault_isolation.h"

#include "camel/core/rtdata/tagged.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/text.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/utils/type.h"

#include <format>
#include <string>

namespace camel::execute {

using namespace camel::core::type;

namespace {

/// The result type of a marked call of a function whose result type is `calleeResult`: `T` gives
/// `T!`, and a function that already returns `U!` gives `U!`, because there is no `U!!`.
Type *isolatedResultType(Type *calleeResult) {
    if (calleeResult->code() == TypeCode::Tuple && tt::as_ptr<TupleType>(calleeResult)->isFallible()) {
        return calleeResult;
    }
    return TupleType::fallible(calleeResult);
}

} // namespace

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
        // An argument stands at the parameter the callee declares for it: a parameter type written
        // without a resource clause takes the resources of the value that meets it.
        if (!sameTypeWithUnwrittenResources(funcType->normTypeAt(i), norm[i + 1])) {
            return std::nullopt;
        }
    }
    return isolatedResultType(funcType->exitType());
}

FunctionType *attemptFunctionType(FunctionType *f) {
    if (f == nullptr || f->withTypesCount() != 0 || !f->hasExitType() || f->hasRest()) {
        return nullptr;
    }
    // The marked call is given values: a parameter the callee writes in place (`var`) cannot be
    // one, exactly as it cannot be an argument of any other operator.
    for (size_t i = 0; i < f->normTypesCount(); ++i) {
        if (f->normIsVarAt(i)) {
            return nullptr;
        }
    }
    // The value is built at the call site out of a callee the type does not name, and a call
    // through it is read as reaching what the callee reaches: the signature publishes no
    // resources rather than the callee's, which the compiler cannot check against the closure it
    // builds (design effects plan 2.2).
    return f->withExitType(isolatedResultType(f->exitType()))
        ->withResources(core::sem::ResourceSignature{});
}

std::optional<Type *> resolveAttemptTransformType(const type_vec_t &with, const type_vec_t &norm) {
    if (!with.empty() || norm.size() != 1 || norm[0] == nullptr ||
        norm[0]->code() != TypeCode::Function) {
        return std::nullopt;
    }
    if (FunctionType *transformed = attemptFunctionType(tt::as_ptr<FunctionType>(norm[0]));
        transformed != nullptr) {
        return transformed;
    }
    return std::nullopt;
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
