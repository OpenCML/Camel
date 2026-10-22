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
 * Updated: Oct. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Filesystem publication consumes only native-owned byte snapshots. A checkpoint directory is
// replaced as a whole: a complete sibling is renamed into place, and the previous directory is
// kept aside until the new one is published (see checkpoint_io.cpp for the protocol).
#pragma once
#include <filesystem>
#include <string>
#include <utility>
#include <vector>
namespace camel::ckptlib {
using CheckpointFiles = std::vector<std::pair<std::string, std::string>>;

/// Writes `files` into a new sibling directory of `directory` and publishes it there as a
/// whole, replacing whatever the path held. Throws (without changing the old directory) when
/// the snapshot cannot be written or published. One writer at a time: a save is not
/// serialized against another save of the same directory, so concurrent writers are not
/// supported (there is no lock file).
void publishCheckpoint(const std::filesystem::path &directory, const CheckpointFiles &files);

/// The directory a reader opens for `directory`: `directory` itself, or its hidden
/// `.<name>.ckpt-previous` sibling when an interrupted save left that as the only complete
/// checkpoint. `recovered` tells which of the two it is. Only reads the file system.
std::filesystem::path
checkpointReadDirectory(const std::filesystem::path &directory, bool &recovered);

std::string readCheckpointFile(const std::filesystem::path &directory, const std::string &name);
} // namespace camel::ckptlib