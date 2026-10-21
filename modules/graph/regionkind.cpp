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
 * Created: Oct. 21, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

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