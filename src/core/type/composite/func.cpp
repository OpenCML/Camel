/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Oct. 06, 2024
 * Updated: Oct. 14, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/type/composite/func.h"
#include "camel/core/mm.h"
#include "camel/core/type/intern.h"
#include "camel/core/type/text.h"
#include "camel/core/type/storage.h"
#include "camel/utils/assert.h"
#include "camel/utils/log.h"

#include <algorithm>
#include <limits>
#include <new>
#include <type_traits>

using namespace std;
using namespace camel::core::type;
using camel::core::internSymbol;
using camel::core::symbolText;
namespace sem = camel::core::sem;

static_assert(
    sizeof(FunctionType) % alignof(Type *) == 0,
    "The parameter arrays follow the fixed part of a FunctionType and must start aligned.");
static_assert(
    sizeof(OpenFunctionType) == sizeof(FunctionType),
    "An open signature has the layout of every other signature.");

// ---- FunctionTypeFactory ----

namespace {

/// The parts a factory collected, its names interned as symbols.
template <typename Parts>
Parts partsOf(
    ModifierSet modifiers, const std::vector<param_t> &withTypes,
    const std::vector<param_t> &normTypes, Type *exitType, bool hasNames,
    const std::vector<std::string> &argNames, bool hasRest, uint16_t constasIndex,
    const sem::ResourceSignature &resources) {
    Parts parts;
    parts.modifiers = modifiers;
    parts.resources = resources;
    parts.params.reserve(withTypes.size() + normTypes.size());
    parts.params.insert(parts.params.end(), withTypes.begin(), withTypes.end());
    parts.params.insert(parts.params.end(), normTypes.begin(), normTypes.end());
    parts.withCount    = withTypes.size();
    parts.exitType     = exitType;
    parts.hasNames     = hasNames;
    parts.hasRest      = hasRest;
    parts.constasIndex = constasIndex;
    if (hasNames) {
        ASSERT(argNames.size() == parts.params.size(), "One name per parameter.");
        parts.names.reserve(argNames.size());
        for (const auto &name : argNames) {
            parts.names.push_back(internSymbol(name));
        }
    }
    return parts;
}

} // namespace

// The factory reaches FunctionType's protected Parts through friendship; the helper above is
// instantiated with it here.
FunctionType *FunctionTypeFactory::build() {
    return FunctionType::intern(partsOf<FunctionType::Parts>(
        modifiers_,
        withTypes_,
        normTypes_,
        exitType_,
        hasNames_,
        argNames_,
        hasRest_,
        constasIndex_,
        resources_));
}

OpenFunctionType *FunctionTypeFactory::buildOpen() {
    return FunctionType::allocate<OpenFunctionType>(partsOf<FunctionType::Parts>(
        modifiers_,
        withTypes_,
        normTypes_,
        exitType_,
        hasNames_,
        argNames_,
        hasRest_,
        constasIndex_,
        resources_));
}

// ---- Construction ----

size_t FunctionType::trailingBytes(size_t params, bool hasNames) {
    return params *
           (sizeof(Type *) + sizeof(TypeCode) + (hasNames ? sizeof(SymId) : 0) + sizeof(uint8_t));
}

FunctionType::FunctionType(const Parts &parts, bool open)
    : CompositeType(TypeCode::Function), exitType_(parts.exitType),
      modifiers_(parts.modifiers), withCount_(static_cast<uint16_t>(parts.withCount)),
      normCount_(static_cast<uint16_t>(parts.params.size() - parts.withCount)),
      hasNames_(parts.hasNames), hasRest_(parts.hasRest),
      constasIndex_(parts.constasIndex), resources_(parts.resources), open_(open) {
    for (size_t i = 0; i < parts.params.size(); ++i) {
        const auto &[type, isVar] = parts.params[i];
        types()[i]                = type;
        codes()[i]                = type->code();
        varMarks()[i]             = isVar ? 1 : 0;
        if (hasNames_) {
            names()[i] = parts.names[i];
        }
    }
}

template <typename T> T *FunctionType::allocate(const Parts &parts) {
    ASSERT(
        parts.withCount <= std::numeric_limits<uint16_t>::max() &&
            parts.params.size() - parts.withCount <= std::numeric_limits<uint16_t>::max(),
        "Too many parameters for one function type.");
    const size_t bytes = sizeof(FunctionType) + trailingBytes(parts.params.size(), parts.hasNames);
    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
        "FunctionType",
        "Allocating {}FunctionType: params={}, totalSize: {} bytes",
        std::is_same_v<T, OpenFunctionType> ? "open " : "",
        parts.params.size(),
        bytes));
    void *mem = allocTypeStorage<FunctionType>(TypeStorageKind::Function, bytes);
    return new (mem) T(parts);
}

// FunctionType itself is constructed through this specialization only, by intern().
template <> FunctionType *FunctionType::allocate<FunctionType>(const Parts &parts) {
    const size_t bytes = sizeof(FunctionType) + trailingBytes(parts.params.size(), parts.hasNames);
    void *mem          = allocTypeStorage<FunctionType>(TypeStorageKind::Function, bytes);
    return new (mem) FunctionType(parts, false);
}

FunctionType *FunctionType::intern(const Parts &parts) {
    // The key is everything a signature consists of (see intern.h; types by address): the
    // counts, the modifiers, the result type, each parameter with its `var` mark, and the names.
    std::vector<uintptr_t> key;
    key.reserve(6 + parts.params.size() * (parts.hasNames ? 3 : 2));
    key.push_back(parts.withCount);
    key.push_back(parts.params.size());
    key.push_back(static_cast<uintptr_t>(parts.modifiers.mask));
    key.push_back(reinterpret_cast<uintptr_t>(parts.exitType));
    key.push_back(parts.hasNames ? 1 : 0);
    key.push_back(parts.hasRest ? 1 : 0);
    key.push_back(parts.constasIndex);
    // The resources are part of what a signature promises: two types that differ only there are
    // not the same type (design effects plan 2.2).
    key.push_back(static_cast<uintptr_t>(parts.resources.mode));
    key.push_back(parts.resources.params.size());
    for (const auto &use : parts.resources.params) {
        key.push_back(use.index);
        key.push_back(static_cast<uintptr_t>(use.cls));
        key.push_back(static_cast<uintptr_t>(use.mode));
        key.push_back(internSymbol(use.name));
    }
    key.push_back(parts.resources.captures.size());
    for (uint8_t capture : parts.resources.captures) {
        key.push_back(capture);
    }
    key.push_back(parts.resources.ambient.size());
    for (const auto &use : parts.resources.ambient) {
        key.push_back(internSymbol(use.name));
        key.push_back(static_cast<uintptr_t>(use.mode));
    }
    key.push_back(parts.resources.anyOf.size());
    for (sem::ResourceClass cls : parts.resources.anyOf) {
        key.push_back(static_cast<uintptr_t>(cls));
    }
    key.push_back(static_cast<uintptr_t>(parts.resources.result.kind));
    key.push_back(parts.resources.result.operand);
    key.push_back(parts.resources.allocates ? 1 : 0);
    if (parts.resources.allocates) {
        key.push_back(static_cast<uintptr_t>(parts.resources.allocates->cls));
        key.push_back(internSymbol(parts.resources.allocates->name));
    }
    key.push_back(parts.resources.external ? 1 : 0);
    for (const auto &[type, isVar] : parts.params) {
        key.push_back(reinterpret_cast<uintptr_t>(type));
        key.push_back(isVar ? 1 : 0);
    }
    if (parts.hasNames) {
        key.insert(key.end(), parts.names.begin(), parts.names.end());
    }
    Type *type = TypeTable::instance().intern(TypeTable::Shape::Function, key, [&]() -> Type * {
        return allocate<FunctionType>(parts);
    });
    return static_cast<FunctionType *>(type);
}

FunctionType::Parts FunctionType::parts() const {
    Parts parts;
    parts.modifiers = modifiers_;
    parts.withCount = withCount_;
    parts.exitType  = exitType_;
    parts.hasNames  = hasNames_;
    parts.hasRest   = hasRest_;
    parts.constasIndex = constasIndex_;
    parts.resources = resources_;
    parts.params.reserve(paramCount());
    for (size_t i = 0; i < paramCount(); ++i) {
        parts.params.emplace_back(types()[i], varMarks()[i] != 0);
        if (hasNames_) {
            parts.names.push_back(names()[i]);
        }
    }
    return parts;
}

FunctionType *FunctionType::create() {
    // What an empty factory builds: no parameters, names recorded (there are none to print).
    FunctionTypeFactory factory;
    return factory.build();
}

FunctionType *FunctionType::create(
    const param_init_list_t &withTypes, const param_init_list_t &normTypes, Type *returnType,
    const ModifierSet &modifiers) {
    return create(param_vec_t(withTypes), param_vec_t(normTypes), returnType, modifiers);
}

FunctionType *FunctionType::create(
    const param_vec_t &withTypes, const param_vec_t &normTypes, Type *returnType,
    const ModifierSet &modifiers) {
    Parts parts;
    parts.modifiers = modifiers;
    parts.params.reserve(withTypes.size() + normTypes.size());
    parts.params.insert(parts.params.end(), withTypes.begin(), withTypes.end());
    parts.params.insert(parts.params.end(), normTypes.begin(), normTypes.end());
    parts.withCount = withTypes.size();
    parts.exitType  = returnType;
    return intern(parts);
}

FunctionType *FunctionType::withExitType(Type *exitType) const {
    Parts changed    = parts();
    changed.exitType = exitType;
    return intern(changed);
}

FunctionType *FunctionType::withResources(const sem::ResourceSignature &resources) const {
    if (resources_ == resources) {
        return const_cast<FunctionType *>(this);
    }
    if (asOpen() != nullptr) {
        OpenFunctionType *copy = OpenFunctionType::copyOf(this);
        copy->setResources(resources);
        return copy;
    }
    Parts changed     = parts();
    changed.resources = resources;
    return intern(changed);
}

OpenFunctionType *FunctionType::asOpen() const {
    return open_ ? static_cast<OpenFunctionType *>(const_cast<FunctionType *>(this)) : nullptr;
}

OpenFunctionType *OpenFunctionType::copyOf(const FunctionType *type) {
    return FunctionType::allocate<OpenFunctionType>(type->parts());
}

// ---- Queries ----

Type *FunctionType::exitType() const {
    // Without a result type yet (an open signature), void; hasExitType() tells the two apart.
    return exitType_ ? exitType_ : Type::Void();
}

bool FunctionType::checkModifiers() const { return true; }

std::string_view FunctionType::argNameAt(size_t idx) const {
    ASSERT(hasNames_, "The function type records no parameter names.");
    ASSERT(idx < paramCount(), "Index out of range");
    return symbolText(names()[idx]);
}

std::vector<std::string_view> FunctionType::argNames() const {
    std::vector<std::string_view> result;
    if (hasNames_) {
        result.reserve(paramCount());
        for (size_t i = 0; i < paramCount(); ++i) {
            result.push_back(symbolText(names()[i]));
        }
    }
    return result;
}

Type *FunctionType::resolve(const type_vec_t &typeList) const {
    ASSERT(false, "Not implemented");
    return nullptr;
}

bool FunctionType::resolved() const { return true; }

string FunctionType::toString() const { return typeToString(this); }

string FunctionType::mangle() const { return typeMangle(this); }

Type *FunctionType::clone(bool deep /* = false */) const {
    Parts copy = parts();
    if (deep) {
        for (auto &[type, isVar] : copy.params) {
            type = type->clone(true);
        }
        if (copy.exitType) {
            copy.exitType = copy.exitType->clone(true);
        }
    }
    return intern(copy);
}

bool FunctionType::equals(Type *other) const { return typeEquals(this, other); }

CastSafety FunctionType::castSafetyFrom(Type *sourceType) const {
    if (sourceType == nullptr)
        return CastSafety::Forbidden;
    if (this == sourceType)
        return CastSafety::Safe;
    return CastSafety::Forbidden;
}

bool FunctionType::assignableFrom(Type *other) const {
    if (this == other)
        return true;
    if (!other || other->code() != TypeCode::Function)
        return false;

    const FunctionType &otherFunc = static_cast<const FunctionType &>(*other);

    if (withCount_ != otherFunc.withCount_ || normCount_ != otherFunc.normCount_ ||
        hasRest_ != otherFunc.hasRest_ || constasIndex_ != otherFunc.constasIndex_)
        return false;

    // Parameters are contravariant: this type's parameter must be acceptable where the other
    // one's is expected.
    for (size_t i = 0; i < paramCount(); i++) {
        if (varMarks()[i] != otherFunc.varMarks()[i] ||
            !otherFunc.types()[i]->assignableFrom(types()[i]))
            return false;
    }

    if (exitType_ && otherFunc.exitType_) {
        if (!exitType_->assignableFrom(otherFunc.exitType_))
            return false;
    } else if (exitType_ || otherFunc.exitType_) {
        return false;
    }

    // Resources (design effects plan 2.2): a target that promises nothing accepts anything; a
    // Concrete target accepts a Concrete source that uses no more (a `pure` function fits
    // anywhere, an arbitrary one does not fit a pure position). A Generic source cannot be
    // proved to fit a Concrete target.
    const sem::ResourceSignature &target = resources_;
    const sem::ResourceSignature &source = otherFunc.resources_;
    if (target.mode == sem::ResourceSignature::Mode::Concrete) {
        if (source.mode == sem::ResourceSignature::Mode::Generic) {
            return false;
        }
        const auto coversMode = [](sem::AccessMode targetMode, sem::AccessMode sourceMode) {
            return targetMode == sem::AccessMode::ReadModifyWrite ||
                   targetMode == sourceMode;
        };
        for (const auto &use : source.params) {
            const bool covered = std::any_of(
                target.params.begin(), target.params.end(), [&](const auto &t) {
                    return t.index == use.index && t.cls == use.cls && t.name == use.name &&
                           coversMode(t.mode, use.mode);
                });
            const bool any = std::any_of(target.anyOf.begin(), target.anyOf.end(), [&](auto cls) {
                return cls == use.cls || cls == sem::ResourceClass::Unknown;
            });
            if (!covered && !any) {
                return false;
            }
        }
        for (const auto &use : source.ambient) {
            const bool covered = std::any_of(
                target.ambient.begin(), target.ambient.end(), [&](const auto &t) {
                    return t.name == use.name && coversMode(t.mode, use.mode);
                });
            if (!covered) {
                return false;
            }
        }
        for (uint8_t capture : source.captures) {
            if (std::find(target.captures.begin(), target.captures.end(), capture) ==
                target.captures.end()) {
                return false;
            }
        }
        for (sem::ResourceClass cls : source.anyOf) {
            if (std::find(target.anyOf.begin(), target.anyOf.end(), cls) == target.anyOf.end()) {
                return false;
            }
        }
        if (source.allocates && !target.allocates) {
            return false;
        }
        if (source.allocates && target.allocates && !(*source.allocates == *target.allocates)) {
            return false;
        }
        if (source.external && !target.external) {
            return false;
        }
    }
    return true;
}

