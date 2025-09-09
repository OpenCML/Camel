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
 * Updated: Jul. 29, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "module.h"
#include "common/error/base.h"

Module::Module(const std::string &name, const std::string &path)
    : loaded_(false), name_(name), path_(path),
      exportedTypeNS_(std::make_shared<Namespace<std::string, type_ptr_t>>()),
      exportedEntityNS_(std::make_shared<Namespace<std::string, entity>>()),
      importedRefModMap_() {};

void Module::importAllRefsFromMod(const module_ptr_t &mod) {
    if (!mod->loaded()) { // 立刻加载
        mod->load();
    }
    // Types
    auto typeNS = mod->exportedTypeNS();
    typeNS->forEach([&](const Reference &ref, const type_ptr_t &type) {
        importedRefModMap_[Reference(ref)] = mod;
    });
    // Entities
    auto entNS = mod->exportedEntityNS();
    entNS->forEach([&](const Reference &ref, const entity &entity) {
        importedRefModMap_[Reference(ref)] = mod;
    });
}
void Module::markImportedRefFromMod(const Reference &ref, const module_ptr_t &mod) {
    importedRefModMap_[ref] = mod;
}
bool Module::hasImportedRef(const Reference &ref) const {
    return importedRefModMap_.find(ref) != importedRefModMap_.end();
}

bool Module::exportType(const Reference &ref, const type_ptr_t &type) {
    return exportedTypeNS_->insert(ref, type);
}
bool Module::exportEntity(const Reference &ref, const entity &ent) {
    return exportedEntityNS_->insert(ref, ent);
}

type_ptr_t Module::getImportedType(const Reference &ref) const {
    ASSERT(
        importedRefModMap_.find(ref) != importedRefModMap_.end(),
        "Imported type not found: " + ref.toString() + " in module " + name_);
    auto &mod = importedRefModMap_.at(ref);
    if (!mod->loaded()) { // 懒加载
        mod->load();
    }
    auto optType = mod->getExportedType(ref);
    if (!optType.has_value()) {
        throw CamelBaseException(
            "Import Error: cannot import type '" + ref.toString() + "' from module '" +
            mod->name() + "'");
    }
    return optType.value();
};
entity Module::getImportedEntity(const Reference &ref) const {
    ASSERT(
        importedRefModMap_.find(ref) != importedRefModMap_.end(),
        "Imported entity not found: " + ref.toString() + " in module " + name_);
    auto &mod = importedRefModMap_.at(ref);
    if (!mod->loaded()) { // 懒加载
        mod->load();
    }
    auto optEntity = mod->getExportedEntity(ref);
    if (!optEntity.has_value()) {
        throw CamelBaseException(
            "Import Error: cannot import entity '" + ref.toString() + "' from module '" +
            mod->name() + "'");
    }
    return optEntity.value();
};

std::optional<type_ptr_t> Module::getExportedType(const Reference &ref) const {
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