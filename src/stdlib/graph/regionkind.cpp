// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "regionkind.h"

#include <iterator>

namespace camel::graphmod {

using camel::core::type::EnumDecl;
using camel::core::type::EnumType;
using camel::core::type::EnumVariant;
using camel::runtime::GCRegionKind;

namespace {

constexpr std::pair<GCRegionKind, const char *> kRegionKinds[] = {
    {GCRegionKind::Root, "Root"},
    {GCRegionKind::Arm, "Arm"},
    {GCRegionKind::LoopCond, "LoopCond"},
    {GCRegionKind::LoopBody, "LoopBody"},
    {GCRegionKind::Inline, "Inline"},
    {GCRegionKind::Block, "Block"},
    {GCRegionKind::Dead, "Dead"},
};

} // namespace

EnumType *regionKindType() {
    static EnumType *type = [] {
        EnumDecl *decl = EnumDecl::create("RegionKind", {});
        std::vector<EnumVariant> variants;
        for (const auto &entry : kRegionKinds) {
            variants.push_back(EnumVariant{.name = entry.second});
        }
        decl->define(std::move(variants));
        return decl->prototype();
    }();
    return type;
}

std::optional<uint32_t> regionKindTag(GCRegionKind kind) {
    for (uint32_t i = 0; i < std::size(kRegionKinds); ++i) {
        if (kRegionKinds[i].first == kind) {
            return i;
        }
    }
    return std::nullopt;
}

const char *regionKindName(uint32_t tag) { return kRegionKinds[tag].second; }

} // namespace camel::graphmod