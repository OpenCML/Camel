/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
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
 * Created: Oct. 19, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Filesystem publication consumes only native-owned byte snapshots.
#pragma once
#include <filesystem>
#include <string>
#include <vector>
namespace camel::treelib {
using CheckpointFiles = std::vector<std::pair<std::string, std::string>>;
void publishCheckpoint(const std::filesystem::path &directory, const CheckpointFiles &files);
std::string readCheckpointFile(const std::filesystem::path &directory, const std::string &name);
} // namespace camel::treelib
