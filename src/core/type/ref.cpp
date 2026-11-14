// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/type/ref.h"

#include "camel/core/mm.h"
#include "camel/core/type/intern.h"
#include "camel/utils/assert.h"

using namespace camel::core::type;

namespace camel::core::type {

namespace {

/// The type code of every `Ref<T>`: one code, so `Ref<int>` and `Ref<float>` are two instances of
/// one type. The value is a handle -- an object the collector traces -- so the code is marked.
TypeCode refTypeCode() {
    static const TypeCode code = registerOtherType("state::Ref", TypeFlag::GC_Traced);
    return code;
}

} // namespace

RefType::RefType(TypeCode code, Type *content)
    : OtherType(
          code,
          content == nullptr ? 0 : 1,
          content == nullptr
              ? nullptr
              : OtherType::copyParams(std::span<Type *const>(&content, std::size_t(1)))) {}

RefType *RefType::get() {
    static RefType *bare = [] {
        const TypeCode code = refTypeCode();
        return static_cast<RefType *>(TypeTable::instance().internOther(
            code, std::span<const uintptr_t>(), [&]() -> Type * {
                void *mem = mm::permSpace().alloc(sizeof(RefType), alignof(RefType));
                ASSERT(mem != nullptr, "Failed to allocate RefType from permSpace");
                return new (mem) RefType(code, nullptr);
            }));
    }();
    return bare;
}

RefType *RefType::of(Type *content) {
    RefType *bare = get();
    if (content == nullptr) {
        return bare;
    }
    const uintptr_t parts[1] = {typeKeyWord(TypeTable::instance().canonical(content))};
    return static_cast<RefType *>(TypeTable::instance().internOther(
        bare->code(), std::span<const uintptr_t>(parts, 1), [&]() -> Type * {
            void *mem = mm::permSpace().alloc(sizeof(RefType), alignof(RefType));
            ASSERT(mem != nullptr, "Failed to allocate RefType from permSpace");
            return new (mem) RefType(bare->code(), content);
        }));
}

std::string RefType::toString() const {
    Type *content = contentType();
    return content == nullptr ? std::string("Ref") : "Ref<" + content->toString() + ">";
}

std::string RefType::mangle() const {
    Type *content = contentType();
    return content == nullptr ? std::string("state.Ref") : "state.Ref_" + content->mangle();
}

Type *RefType::clone(bool deep) const {
    (void)deep;
    return paramCount_ == 1 ? of(params_[0]) : get();
}

Type *RefType::unify(Type *other) const {
    const RefType *have = as(other);
    if (have == nullptr) {
        return nullptr;
    }
    Type *mine   = contentType();
    Type *theirs = have->contentType();
    if (mine == nullptr) {
        return const_cast<RefType *>(have);
    }
    if (theirs == nullptr) {
        return const_cast<RefType *>(this);
    }
    Type *united = mine->unify(theirs);
    return united == nullptr ? nullptr : of(united);
}

bool RefType::equals(Type *other) const {
    const RefType *have = as(other);
    if (have == nullptr) {
        return false;
    }
    Type *mine   = contentType();
    Type *theirs = have->contentType();
    if (mine == nullptr || theirs == nullptr) {
        return mine == theirs;
    }
    return mine->equals(theirs);
}

CastSafety RefType::castSafetyFrom(Type *source) const {
    return is(source) ? CastSafety::Safe : CastSafety::Forbidden;
}

bool RefType::assignableFrom(Type *source) const {
    const RefType *have = as(source);
    if (have == nullptr) {
        return false;
    }
    // A reference written without a content type is one whose content type the program did not
    // state: a handle of a stated content type is taken where none is stated, and one written
    // without one is taken only where none is asked for.
    Type *mine   = contentType();
    Type *theirs = have->contentType();
    if (mine == nullptr || theirs == nullptr) {
        return true;
    }
    return mine->assignableFrom(theirs);
}

OtherType *RefType::cloneWithParams(std::span<Type *const> params) const {
    if (params.empty()) {
        return get();
    }
    if (params.size() != 1) {
        return nullptr;
    }
    return of(params[0]);
}

Type *RefType::cloneWithArguments(std::span<const TypeArgument> arguments, std::string &error) const {
    if (arguments.empty()) {
        return get();
    }
    if (arguments.size() != 1 || arguments[0].type == nullptr) {
        error = "Ref takes one type argument: the type of the value the box holds";
        return nullptr;
    }
    return of(arguments[0].type);
}

} // namespace camel::core::type