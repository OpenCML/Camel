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
 * Created: Oct. 21, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * std::profile (see profile.h).
 */

#include "profile.h"

#include "camel/core/operator_table.h"
#include "camel/core/source/manager.h"
#include "camel/runtime/loop.h"
#include "camel/runtime/reachable.h"

#include <format>
#include <set>
#include <string>
#include <vector>

using camel::runtime::GCGraph;
using camel::runtime::GCNodeKind;
using camel::runtime::LoopView;
using camel::runtime::Program;

namespace {

/// Whether a type name is a tensor (a tensor, or an array of one): what "an element-wise loop
/// over a tensor" means. The type lives in the tensor module, which the core must not name, so
/// the hint reads the name the type prints.
bool tensorNamed(const std::string &name) { return name.find("Tensor") != std::string::npos; }

/// The nodes of one region of `graph`, the regions nested in it included. Mirrors
/// LoopView::regionNodes for a single region.
std::vector<camel::runtime::gc_node_ref_t>
nodesOfRegion(const GCGraph &graph, uint32_t region) {
    std::vector<camel::runtime::gc_node_ref_t> nodes;
    if (region == 0) {
        return nodes;
    }
    const auto inside = [&](uint32_t candidate) {
        for (int guard = 0; candidate != 0 && guard < 4096; ++guard) {
            if (candidate == region) {
                return true;
            }
            const camel::runtime::GCRegion *held = graph.region(candidate);
            candidate                            = held != nullptr ? held->parent : 0;
        }
        return false;
    };
    for (camel::runtime::gc_node_ref_t ref = 0; ref < graph.nodeCount(); ++ref) {
        if (graph.containsNodeRef(ref) && graph.node(ref) != nullptr &&
            inside(graph.nodeRegion(ref))) {
            nodes.push_back(ref);
        }
    }
    return nodes;
}

/// The operator URIs of a set of nodes, once each, in node order.
std::vector<std::string> operatorUris(
    const GCGraph &graph, const std::vector<camel::runtime::gc_node_ref_t> &nodes) {
    std::vector<std::string> uris;
    std::set<std::string> seen;
    for (const camel::runtime::gc_node_ref_t ref : nodes) {
        const camel::runtime::GCNode *node = graph.node(ref);
        if (node == nullptr || node->kind != GCNodeKind::Oper) {
            continue;
        }
        const auto *body = graph.nodeBodyAs<camel::runtime::GCOperBody>(ref);
        if (body == nullptr) {
            continue;
        }
        std::string uri(body->uri());
        if (seen.insert(uri).second) {
            uris.push_back(std::move(uri));
        }
    }
    return uris;
}

std::string joined(const std::vector<std::string> &texts, size_t limit = 6) {
    std::string out;
    for (size_t i = 0; i < texts.size(); ++i) {
        if (i == limit) {
            out += std::format(", ... {} more", texts.size() - limit);
            break;
        }
        out += (i == 0 ? "" : ", ") + texts[i];
    }
    return out;
}

} // namespace

ProfilePass::ProfilePass(
    const camel::core::context::context_ptr_t &ctx, camel::profile::ReportOptions options,
    std::string jsonPath)
    : RuntimeGraphRewritePass(ctx), options_(options), jsonPath_(std::move(jsonPath)) {}

const Program *ProfilePass::apply(const Program *program, std::ostream &os) {
    (void)os;
    camel::profile::enable();
    camel::profile::setReportOptions(options_);
    camel::profile::setJsonPath(jsonPath_);
    if (options_.hints) {
        noteLoopsOf(*program);
    }
    return program;
}

void ProfilePass::noteLoopsOf(const Program &program) {
    if (program.root() == nullptr) {
        return;
    }
    const camel::source::source_context_ptr_t sources = context_->sourceContext();
    for (GCGraph *graph : camel::runtime::collectReachableGraphs(program, program.root())) {
        if (graph == nullptr) {
            continue;
        }
        const std::string graphName = graph->name();
        for (camel::runtime::gc_node_ref_t ref = 0; ref < graph->nodeCount(); ++ref) {
            if (!graph->containsNodeRef(ref) || graph->node(ref) == nullptr ||
                graph->node(ref)->kind != GCNodeKind::Loop) {
                continue;
            }
            LoopView view(program, *graph, ref);
            const std::vector<camel::runtime::gc_node_ref_t> body =
                nodesOfRegion(*graph, view.bodyRegion());
            const std::vector<std::string> uris = operatorUris(*graph, body);

            // Is the body one element-wise chain: every operator of it pure and element-wise, no branch,
// no call of a function, no effect.
            bool elementwise = !uris.empty();
            std::string blocker;
            for (const camel::runtime::gc_node_ref_t node : body) {
                const camel::runtime::GCNode *record = graph->node(node);
                if (record == nullptr) {
                    continue;
                }
                if (record->kind == GCNodeKind::Brch) {
                    elementwise = false;
                    if (blocker.empty()) {
                        blocker = "the body branches";
                    }
                } else if (record->kind == GCNodeKind::Call || record->kind == GCNodeKind::Func) {
                    elementwise = false;
                    if (blocker.empty()) {
                        blocker = "the body calls a function";
                    }
                }
            }
            for (const std::string &uri : uris) {
                const auto traits = camel::core::OperatorTable::instance().find(uri);
                if (!traits.has_value()) {
                    elementwise = false;
                    if (blocker.empty()) {
                        blocker = std::format("`{}` declares no traits", uri);
                    }
                    continue;
                }
                if (!traits->pure || !traits->elementwise) {
                    elementwise = false;
                    if (blocker.empty()) {
                        blocker = std::format("`{}` is not element-wise", uri);
                    }
                }
            }

            // Is the sequence a tensor's elements?
            bool tensor = false;
            if (const auto iterable = view.iterable(); iterable.has_value()) {
                const camel::runtime::GCNode *sequence = graph->node(*iterable);
                if (sequence != nullptr && sequence->kind == GCNodeKind::Oper) {
                    const auto *body_ = graph->nodeBodyAs<camel::runtime::GCOperBody>(*iterable);
                    if (body_ != nullptr && std::string(body_->uri()).starts_with("tensor:")) {
                        tensor = true;
                    }
                }
                if (!tensor && sequence != nullptr && sequence->dataType() != nullptr) {
                    tensor = tensorNamed(sequence->dataType()->toString());
                }
            }
            if (!tensor) {
                if (const auto element = view.bodyElementParam(); element.has_value()) {
                    if (const camel::runtime::GCNode *port = graph->node(*element);
                        port != nullptr && port->dataType() != nullptr) {
                        tensor = tensorNamed(port->dataType()->toString());
                    }
                }
            }

            camel::profile::HostLoopHint hint;
            hint.origin      = graph->nodeOrigin(ref);
            hint.graph       = graphName;
            hint.operators   = joined(uris);
            hint.elementwise = elementwise;
            hint.tensor      = tensor;
            // The place in the source, as the diagnostics of the compiler name it.
            if (sources != nullptr && hint.origin != 0) {
                const camel::core::error::CharRange range = sources->resolveOrigin(hint.origin);
                const std::string path               = sources->pathForOrigin(hint.origin);
                if (!path.empty()) {
                    hint.position = std::format(
                        "{}:{}:{}", path, range.start.line, range.start.character);
                    camel::profile::noteLoopPosition(hint.origin, hint.position);
                }
            }
            if (elementwise) {
                hint.reason = std::format(
                    "no device region covers this loop: only a region a module extracts (tiles) "
                    "runs on a device, and nothing places this one there. Its body is a pure "
                    "element-wise chain of {} operator(s), the shape one kernel can run for a "
                    "whole round (device placement is G1/G2 of docs/design/gpu/README.cn.md).",
                    uris.size());
            } else {
                hint.reason = std::format(
                    "no device region covers this loop, and its body could not be one kernel "
                    "even then: {}. It runs in the host interpreter.",
                    blocker.empty() ? "the body is not a single element-wise chain" : blocker);
            }
            camel::profile::addHostLoopHint(std::move(hint));
        }
    }
}