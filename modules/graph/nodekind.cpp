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

#include "nodekind.h"

#include <iterator>

namespace camel::graphmod {

using camel::core::type::EnumDecl;
using camel::core::type::EnumType;
using camel::core::type::EnumVariant;
using camel::runtime::GCNodeKind;

namespace {

constexpr std::pair<GCNodeKind, const char *> kNodeKinds[] = {
    {GCNodeKind::Data, "Data"},
    {GCNodeKind::Port, "Port"},
    {GCNodeKind::Cast, "Cast"},
    {GCNodeKind::Copy, "Copy"},
    {GCNodeKind::Fill, "Fill"},
    {GCNodeKind::Accs, "Accs"},
    {GCNodeKind::Brch, "Brch"},
    {GCNodeKind::Join, "Join"},
    {GCNodeKind::Call, "Call"},
    {GCNodeKind::Func, "Func"},
    {GCNodeKind::Oper, "Oper"},
    {GCNodeKind::Sync, "Sync"},
    {GCNodeKind::Gate, "Gate"},
    {GCNodeKind::Loop, "Loop"},
};

} // namespace

EnumType *nodeKindType() {
    static EnumType *type = [] {
        EnumDecl *decl = EnumDecl::create("NodeKind", {});
        std::vector<EnumVariant> variants;
        for (const auto &entry : kNodeKinds) {
            variants.push_back(EnumVariant{.name = entry.second});
        }
        decl->define(std::move(variants));
        return decl->prototype();
    }();
    return type;
}

std::optional<uint32_t> nodeKindTag(GCNodeKind kind) {
    switch (kind) {
    case GCNodeKind::Bind:
    case GCNodeKind::Dref:
    case GCNodeKind::Dead:
        return std::nullopt;
    case GCNodeKind::Data:
    case GCNodeKind::Port:
    case GCNodeKind::Cast:
    case GCNodeKind::Copy:
    case GCNodeKind::Fill:
    case GCNodeKind::Accs:
    case GCNodeKind::Brch:
    case GCNodeKind::Join:
    case GCNodeKind::Call:
    case GCNodeKind::Func:
    case GCNodeKind::Oper:
    case GCNodeKind::Sync:
    case GCNodeKind::Gate:
    case GCNodeKind::Loop:
        for (uint32_t i = 0; i < std::size(kNodeKinds); ++i) {
            if (kNodeKinds[i].first == kind) {
                return i;
            }
        }
    }
    return std::nullopt;
}

const char *nodeKindName(uint32_t tag) { return kNodeKinds[tag].second; }

} // namespace camel::graphmod
