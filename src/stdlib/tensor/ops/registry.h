// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Process-wide registry of operator definitions.
 *
 * Modules register their OpDefs under a URI protocol ("tensor", "nn"). The
 * registry then produces, per protocol:
 *   - the OperatorGroups a module exports (one resolver per OpDef, grouped by
 *     export name in registration order, so overloads resolve in that order);
 *   - the URI-suffix -> kernel map its executor serves.
 * Other components (the ONNX exporter, graph passes) look definitions up by
 * full URI.
 */

#pragma once

#include "camel/core/operator_table.h"
#include "op_def.h"

#include <memory>
#include <mutex>
#include <unordered_map>

namespace camel::tensor::ops {

class OpRegistry {
  public:
    static OpRegistry &instance();

    /// Registers definitions under `protocol`. Re-registering a URI is an error.
    void add(std::string_view protocol, std::vector<OpDef> defs);

    /// Definition for a full URI such as "tensor:matmul", or nullptr.
    const OpDef *find(std::string_view uri) const;
    /// Definition for a graph's process-local operator identity, or nullptr.
    const OpDef *find(core::OperId id) const;

    std::vector<oper_group_ptr_t> operatorGroups(std::string_view protocol) const;
    std::unordered_map<std::string, operator_t> kernelMap(std::string_view protocol) const;

  private:
    struct Entry {
        std::string protocol;
        std::string uri;
        std::shared_ptr<const OpDef> def;
    };

    mutable std::mutex mutex_;
    std::vector<Entry> entries_; // registration order
    std::unordered_map<std::string, size_t> byUri_;
    std::unordered_map<core::OperId, size_t> byId_;
};

/// Registers the tensor module's own operators (idempotent).
void registerTensorOps();

} // namespace camel::tensor::ops
