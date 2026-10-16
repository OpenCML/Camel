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
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The resource declarations of operators (see resource_semantics.h): names, builders, the
 * conservative reading, the consistency check and the text form.
 */

#include "camel/core/resource_semantics.h"

#include <format>

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

ResourceAccess operandAccess(ResourceClass cls, uint8_t operand, AccessMode mode, std::string name) {
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

ResourceSpec conservativeResources(size_t normCount, size_t withCount, bool pure, bool externalEffect) {
    ResourceSpec spec;
    if (pure) {
        return spec;
    }
    // Every operand may name a resource the operator reads and writes, whole; the result may be
    // any of them. Indexes past a byte are not representable, and no operator has that many.
    for (size_t i = 0; i < normCount && i < 0xFF; ++i) {
        spec.accesses.push_back(ResourceAccess{
            .resource = Resource{},
            .binding  = ResourceBinding{.kind = ResourceBinding::Kind::Norm, .index = uint8_t(i)},
            .region   = ResourceRegion{.kind = ResourceRegion::Kind::Unknown},
            .mode     = AccessMode::ReadModifyWrite,
        });
    }
    for (size_t i = 0; i < withCount && i < 0xFF; ++i) {
        spec.accesses.push_back(ResourceAccess{
            .resource = Resource{},
            .binding  = ResourceBinding{.kind = ResourceBinding::Kind::With, .index = uint8_t(i)},
            .region   = ResourceRegion{.kind = ResourceRegion::Kind::Unknown},
            .mode     = AccessMode::ReadModifyWrite,
        });
    }
    spec.result         = ResultAlias{.kind = ResultAlias::Kind::Unknown};
    spec.externalEffect = externalEffect;
    return spec;
}

std::optional<std::string> checkResources(const ResourceSpec &spec) {
    const auto named = [](ResourceClass cls) {
        return cls == ResourceClass::Foreign || cls == ResourceClass::Ambient;
    };
    for (const ResourceAccess &access : spec.accesses) {
        const Resource &r = access.resource;
        if (r.cls == ResourceClass::Unknown) {
            return std::string("an access names the class of its resource (unknown is the reading "
                               "of an undeclared operator, not a declaration)");
        }
        if (named(r.cls) && r.name.empty()) {
            return std::format("a resource of class {} has a name", resourceClassName(r.cls));
        }
        if (!named(r.cls) && !r.name.empty()) {
            return std::format(
                "a resource of class {} has no name ('{}')", resourceClassName(r.cls), r.name);
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
            return std::string("an operator allocates a variable, an object or a foreign resource");
        }
        if (named(spec.allocates->cls) == spec.allocates->name.empty()) {
            return std::string("the resource it allocates is named exactly when it is foreign");
        }
    }
    const ResultAlias::Kind k = spec.result.kind;
    if ((k == ResultAlias::Kind::Same || k == ResultAlias::Kind::View) && spec.result.operand == 0xFF) {
        return std::format("a result that is {} names its operand", resultAliasName(k));
    }
    return std::nullopt;
}

std::string toString(const ResourceAccess &access) {
    std::string out = std::format(
        "{} {}",
        accessModeName(access.mode),
        resourceClassName(access.resource.cls));
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

} // namespace camel::core::sem
