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
 * Author: Camel Contributors
 * Created: Sep. 30, 2026
 * Updated: Sep. 30, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * What an ONNX runtime can execute: for each operator, the element types and
 * opsets its execution provider has kernels for. The data is declarative
 * (capabilities/onnxruntime-cpu.json, produced by probing the runtime with
 * benchmarks/tools/probe_ort_capabilities.py) and embedded at build time. The
 * emitter consults it for every node, so a model that is valid ONNX but that
 * the target runtime cannot load is rejected at export time, at the source
 * construct that produced the node, rather than when the runtime opens it.
 */

#pragma once

#include "proto/onnx_writer.h"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace camel::onnx {

class CapabilityTable {
  public:
    /// The table of `target` ("onnxruntime-cpu"), or nullptr for an unknown target.
    static const CapabilityTable *forTarget(const std::string &target);
    /// Every target with capability data.
    static std::vector<std::string> targets();

    const std::string &runtime() const { return runtime_; }
    const std::string &runtimeVersion() const { return runtimeVersion_; }
    const std::string &provider() const { return provider_; }

    /// Why the target cannot run `opType` on `elem` inputs at `opset`, or nullopt when it can
    /// (or when the table does not cover the operator).
    std::optional<std::string> rejection(const std::string &opType, ElemType elem, int64_t opset) const;

    /// Operators the target runs at `opset`, with the element types it runs them on.
    std::map<std::string, std::vector<std::string>> supportedAt(int64_t opset) const;

  private:
    struct OpEntry {
        std::map<std::string, std::set<int64_t>> supported; // element type -> opsets
        std::map<std::string, std::set<int64_t>> noKernel;
    };

    static CapabilityTable parse(std::string_view json);

    std::string runtime_;
    std::string runtimeVersion_;
    std::string provider_;
    std::map<std::string, OpEntry> ops_;
};

/// Capability name of an element type ("float32", "int64", "bool").
std::string elemTypeName(ElemType elem);

} // namespace camel::onnx
