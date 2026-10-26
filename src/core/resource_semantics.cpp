// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The resource declarations of operators (see resource_semantics.h): names, builders, the
 * conservative reading, the consistency check and the text form.
 */

#include "camel/core/resource_semantics.h"

#include <algorithm>
#include <array>
#include <format>

#include "camel/core/type/base.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/enum.h"

namespace camel::core::sem {

std::string_view resourceClassName(ResourceClass cls) {
    switch (cls) {
    case ResourceClass::Unknown:
        return "unknown";
    case ResourceClass::Variable:
        return "variable";
    case ResourceClass::Object:
        return "object";
    case ResourceClass::Foreign:
        return "foreign";
    case ResourceClass::Ambient:
        return "ambient";
    case ResourceClass::Handle:
        return "handle";
    }
    return "unknown";
}

std::string_view accessModeName(AccessMode mode) {
    switch (mode) {
    case AccessMode::Read:
        return "read";
    case AccessMode::Write:
        return "write";
    case AccessMode::ReadModifyWrite:
        return "rmw";
    }
    return "rmw";
}

std::string_view resultAliasName(ResultAlias::Kind kind) {
    switch (kind) {
    case ResultAlias::Kind::Fresh:
        return "fresh";
    case ResultAlias::Kind::Same:
        return "same";
    case ResultAlias::Kind::View:
        return "view";
    case ResultAlias::Kind::MayAlias:
        return "may_alias";
    case ResultAlias::Kind::Unknown:
        return "unknown";
    }
    return "unknown";
}

ResourceAccess
operandAccess(ResourceClass cls, uint8_t operand, AccessMode mode, std::string name) {
    return ResourceAccess{
        .resource = Resource{.cls = cls, .name = std::move(name)},
        .binding  = ResourceBinding{.kind = ResourceBinding::Kind::Norm, .index = operand},
        .region   = ResourceRegion{},
        .mode     = mode,
    };
}

ResourceAccess ambientAccess(std::string name, AccessMode mode) {
    return ResourceAccess{
        .resource = Resource{.cls = ResourceClass::Ambient, .name = std::move(name)},
        .binding  = ResourceBinding{.kind = ResourceBinding::Kind::Ambient},
        .region   = ResourceRegion{},
        .mode     = mode,
    };
}

ResourceSpec externalAmbient(std::string name, AccessMode mode) {
    return ResourceSpec{.accesses = {ambientAccess(std::move(name), mode)}, .externalEffect = true};
}

ResourceSpec
conservativeResources(size_t normCount, size_t withCount, bool pure, bool externalEffect) {
    ResourceSpec spec;
    if (pure) {
        return spec;
    }
    // Every operand may name a resource the operator reads and writes, whole; the result may be
    // any of them. Indexes past a byte are not representable, and no operator has that many.
    for (size_t i = 0; i < normCount && i < 0xFF; ++i) {
        spec.accesses.push_back(
            ResourceAccess{
                .resource = Resource{},
                .binding =
                    ResourceBinding{.kind = ResourceBinding::Kind::Norm, .index = uint8_t(i)},
                .region = ResourceRegion{.kind = ResourceRegion::Kind::Unknown},
                .mode   = AccessMode::ReadModifyWrite,
            });
    }
    for (size_t i = 0; i < withCount && i < 0xFF; ++i) {
        spec.accesses.push_back(
            ResourceAccess{
                .resource = Resource{},
                .binding =
                    ResourceBinding{.kind = ResourceBinding::Kind::With, .index = uint8_t(i)},
                .region = ResourceRegion{.kind = ResourceRegion::Kind::Unknown},
                .mode   = AccessMode::ReadModifyWrite,
            });
    }
    spec.result         = ResultAlias{.kind = ResultAlias::Kind::Unknown};
    spec.externalEffect = externalEffect;
    return spec;
}

bool writesOperand(const ResourceSpec &spec, bool isWith, size_t index) {
    const auto kind = isWith ? ResourceBinding::Kind::With : ResourceBinding::Kind::Norm;
    for (const ResourceAccess &access : spec.accesses) {
        if (access.binding.kind == kind && access.binding.index == index && writes(access.mode)) {
            return true;
        }
    }
    return false;
}

bool touchesClass(const ResourceSpec &spec, ResourceClass cls, bool writesOnly) {
    for (const ResourceAccess &access : spec.accesses) {
        if (access.resource.cls == cls && (!writesOnly || writes(access.mode))) {
            return true;
        }
    }
    return false;
}

std::optional<std::string> checkVarParameters(
    const ResourceSpec &spec, const std::vector<bool> &withVars,
    const std::vector<bool> &normVars) {
    for (const bool isWith : {false, true}) {
        const std::vector<bool> &vars = isWith ? withVars : normVars;
        for (size_t i = 0; i < vars.size(); ++i) {
            const bool written = writesOperand(spec, isWith, i);
            if (vars[i] && !written) {
                return std::format(
                    "the signature has a var {} parameter {} that the declaration does not write",
                    isWith ? "with" : "ordinary",
                    i);
            }
            if (!vars[i] && !isWith) {
                // A cell or an in-place object is written through a var parameter only.
                for (const ResourceAccess &access : spec.accesses) {
                    if (access.binding.kind == ResourceBinding::Kind::Norm &&
                        access.binding.index == i && writes(access.mode) &&
                        (access.resource.cls == ResourceClass::Variable ||
                         access.resource.cls == ResourceClass::Object)) {
                        return std::format(
                            "the declaration writes a {} through ordinary parameter {}, which is "
                            "not a var parameter",
                            resourceClassName(access.resource.cls),
                            i);
                    }
                }
            }
        }
    }
    return std::nullopt;
}

std::optional<std::string> checkResources(const ResourceSpec &spec) {
    const auto named = [](ResourceClass cls) {
        return cls == ResourceClass::Foreign || cls == ResourceClass::Ambient ||
               cls == ResourceClass::Handle;
    };
    for (const ResourceAccess &access : spec.accesses) {
        const Resource &r = access.resource;
        if (r.cls == ResourceClass::Unknown) {
            return std::string(
                "an access names the class of its resource (unknown is the reading "
                "of an undeclared operator, not a declaration)");
        }
        if (named(r.cls) && r.name.empty()) {
            return std::format("a resource of class {} has a name", resourceClassName(r.cls));
        }
        if (!named(r.cls) && !r.name.empty()) {
            return std::format(
                "a resource of class {} has no name ('{}')",
                resourceClassName(r.cls),
                r.name);
        }
        const bool ambientBinding = access.binding.kind == ResourceBinding::Kind::Ambient;
        if ((r.cls == ResourceClass::Ambient) != ambientBinding) {
            return std::string(
                ambientBinding ? "only an ambient resource is named by no operand"
                               : "an ambient resource is named by no operand");
        }
        if (access.binding.kind == ResourceBinding::Kind::Capture) {
            return std::string("an operator's access is bound to an operand, not a capture");
        }
    }
    if (spec.allocates) {
        if (spec.allocates->cls == ResourceClass::Unknown ||
            spec.allocates->cls == ResourceClass::Ambient) {
            return std::string(
                "an operator allocates a variable, an object, a handle or a foreign resource");
        }
        if (named(spec.allocates->cls) == spec.allocates->name.empty()) {
            return std::string(
                "the resource it allocates is named exactly when it is a handle or foreign");
        }
    }
    const ResultAlias::Kind k = spec.result.kind;
    if ((k == ResultAlias::Kind::Same || k == ResultAlias::Kind::View) &&
        spec.result.operand == 0xFF) {
        return std::format("a result that is {} names its operand", resultAliasName(k));
    }
    return std::nullopt;
}

std::string toString(const ResourceAccess &access) {
    std::string out =
        std::format("{} {}", accessModeName(access.mode), resourceClassName(access.resource.cls));
    if (!access.resource.name.empty()) {
        out += " " + access.resource.name;
    }
    switch (access.binding.kind) {
    case ResourceBinding::Kind::Norm:
        out += std::format(" norm {}", access.binding.index);
        break;
    case ResourceBinding::Kind::With:
        out += std::format(" with {}", access.binding.index);
        break;
    case ResourceBinding::Kind::Capture:
        out += std::format(" capture {}", access.binding.index);
        break;
    case ResourceBinding::Kind::Ambient:
        break;
    }
    switch (access.region.kind) {
    case ResourceRegion::Kind::Whole:
        if (access.resource.cls != ResourceClass::Ambient) {
            out += " whole";
        }
        break;
    case ResourceRegion::Kind::Unknown:
        out += " region unknown";
        break;
    case ResourceRegion::Kind::Mapped:
        switch (access.region.map) {
        case ResourceRegion::Map::Norm:
            out += std::format(" region of norm {}", access.region.operand);
            break;
        case ResourceRegion::Map::With:
            out += std::format(" region of with {}", access.region.operand);
            break;
        case ResourceRegion::Map::Write:
            out += " region of the write";
            break;
        }
        break;
    }
    return out;
}

std::string toString(const ResourceSpec &spec) {
    if (spec.none()) {
        return "none";
    }
    std::string out;
    const auto part = [&](const std::string &text) { out += (out.empty() ? "" : "; ") + text; };
    for (const ResourceAccess &access : spec.accesses) {
        part(toString(access));
    }
    if (spec.result.kind != ResultAlias::Kind::Fresh) {
        std::string result = std::format("result {}", resultAliasName(spec.result.kind));
        if (spec.result.operand != 0xFF) {
            result += std::format(" {}", spec.result.operand);
        }
        part(result);
    }
    if (spec.allocates) {
        std::string allocates = std::format("allocates {}", resourceClassName(spec.allocates->cls));
        if (!spec.allocates->name.empty()) {
            allocates += " " + spec.allocates->name;
        }
        part(allocates);
    }
    if (spec.externalEffect) {
        part("external");
    }
    return out;
}

ResourceSignature signatureOf(const ResourceSpec &spec, size_t withCount) {
    ResourceSignature sig;
    sig.mode    = ResourceSignature::Mode::Concrete;
    sig.result  = spec.result;
    sig.allocates = spec.allocates;
    sig.external  = spec.externalEffect;
    const auto uniteMode = [](AccessMode a, AccessMode b) {
        return writes(a) || writes(b) ? AccessMode::ReadModifyWrite : AccessMode::Read;
    };
    for (const ResourceAccess &access : spec.accesses) {
        switch (access.binding.kind) {
        case ResourceBinding::Kind::Norm:
        case ResourceBinding::Kind::With: {
            // The signature numbers with parameters first, ordinary ones after them; a spec
            // numbers the two kinds separately.
            const size_t index =
                size_t(access.binding.index) +
                (access.binding.kind == ResourceBinding::Kind::With ? 0 : withCount);
            if (index > 0xFF) {
                break;
            }
            auto found = std::find_if(sig.params.begin(), sig.params.end(), [&](const auto &use) {
                return use.index == index;
            });
            if (found == sig.params.end()) {
                sig.params.push_back(
                    ResourceSignature::ParamUse{
                        .index = static_cast<uint8_t>(index),
                        .cls   = access.resource.cls,
                        .name  = access.resource.name,
                        .mode  = access.mode});
            } else {
                // One parameter, one resource: two declarations of it are one use.
                if (found->cls != access.resource.cls) {
                    found->cls = ResourceClass::Unknown;
                }
                if (found->name.empty()) {
                    found->name = access.resource.name;
                }
                found->mode = uniteMode(found->mode, access.mode);
            }
            break;
        }
        case ResourceBinding::Kind::Ambient: {
            auto found = std::find_if(sig.ambient.begin(), sig.ambient.end(), [&](const auto &use) {
                return use.name == access.resource.name;
            });
            if (found == sig.ambient.end()) {
                sig.ambient.push_back(
                    ResourceSignature::AmbientUse{
                        .name = access.resource.name, .mode = access.mode});
            } else {
                found->mode = uniteMode(found->mode, access.mode);
            }
            break;
        }
        case ResourceBinding::Kind::Capture:
            sig.captures.push_back(access.binding.index);
            break;
        }
    }
    std::sort(sig.params.begin(), sig.params.end(), [](const auto &a, const auto &b) {
        return a.index < b.index;
    });
    std::sort(sig.ambient.begin(), sig.ambient.end(), [](const auto &a, const auto &b) {
        return a.name < b.name;
    });
    std::sort(sig.captures.begin(), sig.captures.end());
    sig.captures.erase(std::unique(sig.captures.begin(), sig.captures.end()), sig.captures.end());
    return sig;
}

bool covers(const ResourceSignature &target, const ResourceSignature &source) {
    if (target.mode == ResourceSignature::Mode::Generic) {
        return true; // a signature that promises nothing asks for nothing
    }
    if (source.mode == ResourceSignature::Mode::Generic) {
        return false; // nothing is known about the source, so it is not proved to fit
    }
    // `read-modify-write` at the target allows either mode at the source: the source may read
    // less than it is allowed to. The other direction is not symmetric, which is why the mode is
    // compared with this helper rather than for equality.
    const auto coversMode = [](AccessMode targetMode, AccessMode sourceMode) {
        return targetMode == AccessMode::ReadModifyWrite || targetMode == sourceMode;
    };
    for (const auto &use : source.params) {
        const bool covered = std::any_of(
            target.params.begin(), target.params.end(), [&](const auto &t) {
                return t.index == use.index && t.cls == use.cls && t.name == use.name &&
                       coversMode(t.mode, use.mode);
            });
        const bool any = std::any_of(target.anyOf.begin(), target.anyOf.end(), [&](auto cls) {
            return cls == use.cls || cls == ResourceClass::Unknown;
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
    for (ResourceClass cls : source.anyOf) {
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
    return true;
}

std::span<const std::string_view> standardResourceNames() {
    // Design principles 3.3: the state the language gives every program, with the names a
    // declaration uses for it. `rng`, `tensor:generator` and the other module resources are
    // declared by their modules, not here.
    static constexpr std::array<std::string_view, 6> names{
        "stdout", "stderr", "stdin", "clock", "env", "fs"};
    return names;
}

bool isStandardResource(std::string_view name) {
    const std::span<const std::string_view> names = standardResourceNames();
    return std::find(names.begin(), names.end(), name) != names.end();
}

bool isHandleType(const type::Type *type) {
    if (type == nullptr) {
        return false;
    }
    if (type->code() == type::TypeCode::Struct) {
        const auto *structType = static_cast<const type::StructType *>(type);
        return structType->decl() != nullptr && structType->decl()->handle();
    }
    if (type->code() == type::EnumType::typeCode()) {
        const auto *enumType = static_cast<const type::EnumType *>(type);
        return enumType->decl() != nullptr && enumType->decl()->handle();
    }
    return false;
}

std::string handleResourceName(const type::Type *type) {
    if (!isHandleType(type)) {
        return {};
    }
    std::string name;
    std::string module;
    if (type->code() == type::TypeCode::Struct) {
        const type::StructDecl *decl = static_cast<const type::StructType *>(type)->decl();
        name                         = decl->name();
        module                       = decl->moduleName();
    } else {
        const type::EnumDecl *decl = static_cast<const type::EnumType *>(type)->decl();
        name                       = decl->name();
        module                     = decl->moduleName();
    }
    return module.empty() ? name : module + "::" + name;
}

std::string toString(const ResourceSignature &signature) {
    if (signature.mode == ResourceSignature::Mode::Generic) {
        return {};
    }
    if (signature.pure()) {
        return "pure";
    }
    std::string out = "uses ";
    bool first      = true;
    const auto add  = [&](const std::string &name) {
        out += (first ? "" : ", ") + name;
        first = false;
    };
    for (const auto &use : signature.params) {
        add(std::format("#{}", use.index));
    }
    for (const auto &use : signature.ambient) {
        add(use.name);
    }
    for (ResourceClass cls : signature.anyOf) {
        add(std::format("any {}", resourceClassName(cls)));
    }
    for (uint8_t capture : signature.captures) {
        add(std::format("capture {}", capture));
    }
    return out;
}

} // namespace camel::core::sem
