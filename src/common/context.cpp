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
 * Created: Aug. 18, 2024
 * Updated: Mar. 10, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "context.h"

std::string resolveModuleName(const std::string &currentModule, const std::string &importName) {
    if (importName.empty() || importName[0] != '.') {
        return importName;
    }

    int level = 0;
    size_t i = 0;
    while (i < importName.size() && importName[i] == '.') {
        ++level;
        ++i;
    }

    std::string remaining = importName.substr(i);

    std::vector<std::string> base = split(currentModule, '.');
    if (base.size() < level) {
        throw std::runtime_error("Too many dots in relative import");
    }

    base.resize(base.size() - level); // 向上走 level 层
    if (!remaining.empty()) {
        std::vector<std::string> rest = split(remaining, '.');
        base.insert(base.end(), rest.begin(), rest.end());
    }

    return join(base, '.'); // 返回逻辑模块名
}
