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

Module::Module(const std::string &name, const std::string &path)
    : name_(name), path_(path),
      importedTypeNS_(std::make_shared<Namespace<std::string, type_ptr_t>>()),
      exportedTypeNS_(std::make_shared<Namespace<std::string, type_ptr_t>>()),
      importedEntityNS_(std::make_shared<Namespace<std::string, entity>>()),
      exportedEntityNS_(std::make_shared<Namespace<std::string, entity>>()) {};

void Module::importEntities(const module_ptr_t &mod, const std::vector<Reference> &refs) {
    imports_.push_back(mod);
    if (refs.empty()) {
        // import * from module
        importedTypeNS_->insertAllFrom(*mod->exportedTypes(), false);
        importedEntityNS_->insertAllFrom(*mod->exportedEntitys(), false);
    } else {
        for (const auto &ref : refs) {
            if (auto type = mod->getExportedType(ref); type.has_value()) {
                importedTypeNS_->insert(ref, type.value());
            } else if (auto ent = mod->getExportedEntity(ref); ent.has_value()) {
                importedEntityNS_->insert(ref, ent.value());
            }
        }
    }
}