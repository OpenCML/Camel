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
 * Embedded capability data of export targets (see capability.h).
 */

#include "capability.h"

#include "emitter.h"

#include <nlohmann/json.hpp>

#include <format>
#include <string_view>

namespace camel::onnx {

namespace {

const unsigned char kOnnxRuntimeCpu[] = {
#include "onnxruntime_cpu_capabilities.inc"
};

std::string_view embedded(const unsigned char *data, size_t size) {
    return {reinterpret_cast<const char *>(data), size};
}

std::string joinOpsets(const std::set<int64_t> &opsets) {
    std::string out;
    for (int64_t o : opsets) {
        out += (out.empty() ? "" : ", ") + std::to_string(o);
    }
    return out;
}

} // namespace

std::string elemTypeName(ElemType elem) {
    switch (elem) {
    case ElemType::Float:
        return "float32";
    case ElemType::Int64:
        return "int64";
    case ElemType::Bool:
        return "bool";
    case ElemType::Int32:
        return "int32";
    case ElemType::Double:
        return "float64";
    }
    return "unknown";
}

CapabilityTable CapabilityTable::parse(std::string_view text) {
    const auto json = nlohmann::json::parse(text);
    CapabilityTable table;
    table.runtime_        = json.at("runtime").get<std::string>();
    table.runtimeVersion_ = json.at("runtime_version").get<std::string>();
    table.provider_       = json.at("provider").get<std::string>();
    for (const auto &[op, entry] : json.at("ops").items()) {
        OpEntry &e = table.ops_[op];
        for (const auto &[kind, target] :
             {std::pair{"supported", &e.supported}, std::pair{"no_kernel", &e.noKernel}}) {
            if (!entry.contains(kind)) {
                continue;
            }
            for (const auto &[elem, opsets] : entry.at(kind).items()) {
                for (const auto &o : opsets) {
                    (*target)[elem].insert(o.get<int64_t>());
                }
            }
        }
    }
    return table;
}

const CapabilityTable *CapabilityTable::forTarget(const std::string &target) {
    if (target == "onnxruntime-cpu") {
        static const CapabilityTable table =
            parse(embedded(kOnnxRuntimeCpu, sizeof(kOnnxRuntimeCpu)));
        return &table;
    }
    return nullptr;
}

std::vector<std::string> CapabilityTable::targets() { return {"onnxruntime-cpu"}; }

std::optional<std::string>
CapabilityTable::rejection(const std::string &opType, ElemType elem, int64_t opset) const {
    auto it = ops_.find(opType);
    if (it == ops_.end()) {
        return std::nullopt; // not covered by the probe
    }
    const std::string name = elemTypeName(elem);
    const OpEntry &e       = it->second;
    if (auto s = e.supported.find(name); s != e.supported.end() && s->second.contains(opset)) {
        return std::nullopt;
    }
    const std::string where = std::format("{} {} ({})", runtime_, runtimeVersion_, provider_);
    if (auto n = e.noKernel.find(name); n != e.noKernel.end() && n->second.contains(opset)) {
        std::string hint;
        if (auto s = e.supported.find(name); s != e.supported.end() && !s->second.empty()) {
            hint = std::format("; it has one at opset {}", joinOpsets(s->second));
        }
        return std::format(
            "{} has no kernel for {} on {} tensors at opset {}: the model would be valid ONNX but "
            "fail to load{}",
            where,
            opType,
            name,
            opset,
            hint);
    }
    return std::format("ONNX opset {} does not define {} on {} tensors", opset, opType, name);
}

std::map<std::string, std::vector<std::string>> CapabilityTable::supportedAt(int64_t opset) const {
    std::map<std::string, std::vector<std::string>> out;
    for (const auto &[op, e] : ops_) {
        for (const auto &[elem, opsets] : e.supported) {
            if (opsets.contains(opset)) {
                out[op].push_back(elem);
            }
        }
    }
    return out;
}

} // namespace camel::onnx
