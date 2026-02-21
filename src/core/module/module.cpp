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
 * Created: Jul. 29, 2025
 * Updated: Feb. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/module/module.h"
#include "camel/compile/gir.h"
#include "camel/core/operator.h"

#include <algorithm>

namespace {

/// 将多个模块导出的同名 entity 合并为一个，用于支持多模块下的函数/算子重载。
/// - graph_vec_ptr_t: 合并所有图的向量
/// - oper_group_ptr_t: 合并所有算子组的 resolvers（按同名 group 合并）
/// - node_ptr_t: 仅取第一个
std::optional<entity> mergeImportedEntities(const std::vector<entity> &entities) {
    if (entities.empty()) {
        return std::nullopt;
    }
    const entity &first = entities.front();
    if (std::holds_alternative<GraphIR::graph_vec_ptr_t>(first)) {
        auto merged = std::make_shared<GraphIR::graph_vec_t>();
        for (const auto &ent : entities) {
            if (auto *pv = std::get_if<GraphIR::graph_vec_ptr_t>(&ent)) {
                if (*pv) {
                    for (const auto &g : **pv) {
                        merged->push_back(g);
                    }
                }
            }
        }
        return merged;
    }
    if (std::holds_alternative<oper_group_ptr_t>(first)) {
        std::string name;
        std::vector<std::pair<std::string, resolver_ptr_t>> allResolvers;
        for (const auto &ent : entities) {
            if (auto *pg = std::get_if<oper_group_ptr_t>(&ent)) {
                if (*pg) {
                    if (name.empty()) {
                        name = (*pg)->name();
                    }
                    for (const auto &r : (*pg)->resolvers()) {
                        allResolvers.push_back(r);
                    }
                }
            }
        }
        if (name.empty()) {
            return std::nullopt;
        }
        return OperatorGroup::create(name, std::move(allResolvers));
    }
    // node_ptr_t 或其它：返回第一个
    return first;
}

} // namespace

Module::Module(const std::string &name, const std::string &path, context_ptr_t ctx)
    : loaded_(false), name_(name), path_(path), context_(ctx),
      exportedTypeNS_(std::make_shared<Namespace<std::string, Type *>>()),
      exportedEntityNS_(std::make_shared<Namespace<std::string, entity>>()), importedRefModMap_(),
      importedEntityCache_() {}

void Module::importAllRefsFromMod(const module_ptr_t &mod) {
    if (!mod->loaded()) {
        mod->load();
    }
    auto appendModForRef = [this, &mod](const Reference &ref) {
        auto &vec = importedRefModMap_[ref];
        if (std::find(vec.begin(), vec.end(), mod) == vec.end()) {
            vec.push_back(mod);
        }
    };
    auto typeNS = mod->exportedTypeNS();
    typeNS->forEach([&](const Reference &ref, Type *) { appendModForRef(ref); });
    auto entNS = mod->exportedEntityNS();
    entNS->forEach([&](const Reference &ref, const entity &) { appendModForRef(ref); });
    importedEntityCache_.clear();
}

void Module::markImportedRefFromMod(const Reference &ref, const module_ptr_t &mod) {
    auto &vec = importedRefModMap_[ref];
    if (std::find(vec.begin(), vec.end(), mod) == vec.end()) {
        vec.push_back(mod);
    }
    importedEntityCache_.erase(ref);
}

bool Module::hasImportedRef(const Reference &ref) const {
    auto it = importedRefModMap_.find(ref);
    return it != importedRefModMap_.end() && !it->second.empty();
}

bool Module::exportType(const Reference &ref, Type *type) {
    return exportedTypeNS_->insert(ref, type);
}

bool Module::exportEntity(const Reference &ref, const entity &ent) {
    return exportedEntityNS_->insert(ref, ent);
}

std::optional<Type *> Module::getImportedType(const Reference &ref) const {
    auto it = importedRefModMap_.find(ref);
    if (it == importedRefModMap_.end() || it->second.empty()) {
        return std::nullopt;
    }
    // Type 无重载语义，取第一个提供该 ref 的模块即可
    auto &mod = it->second.front();
    if (!mod->loaded()) {
        mod->load();
    }
    return mod->getExportedType(ref);
}

std::optional<entity> Module::getImportedEntity(const Reference &ref) const {
    auto it = importedRefModMap_.find(ref);
    if (it == importedRefModMap_.end() || it->second.empty()) {
        return std::nullopt;
    }
    // 命中缓存则直接返回，避免重复合并
    auto cacheIt = importedEntityCache_.find(ref);
    if (cacheIt != importedEntityCache_.end()) {
        return cacheIt->second;
    }
    std::vector<entity> collected;
    for (const auto &mod : it->second) {
        if (!mod->loaded()) {
            mod->load();
        }
        auto opt = mod->getExportedEntity(ref);
        if (opt) {
            collected.push_back(std::move(*opt));
        }
    }
    auto merged = mergeImportedEntities(collected);
    if (merged) {
        importedEntityCache_[ref] = *merged;
    }
    return merged;
}

std::optional<Type *> Module::getExportedType(const Reference &ref) const {
    ASSERT(loaded_, "Module not built: " + name_);
    return exportedTypeNS_->get(ref);
};

std::optional<entity> Module::getExportedEntity(const Reference &ref) const {
    ASSERT(loaded_, "Module not built: " + name_);
    return exportedEntityNS_->get(ref);
};

type_ns_ptr_t Module::exportedTypeNS() const {
    ASSERT(loaded_, "Module not built: " + name_);
    return exportedTypeNS_;
}

entity_ns_ptr_t Module::exportedEntityNS() const {
    ASSERT(loaded_, "Module not built: " + name_);
    return exportedEntityNS_;
}
