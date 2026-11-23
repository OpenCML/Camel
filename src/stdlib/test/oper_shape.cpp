// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The pass of oper_shape.h: one line per form the program uses, in sorted order, and nothing else.
 *
 * The reading is a *set* rather than a count, so a graph this pass does not own (a library's, or a
 * node an unrelated edit adds) contributes no line of its own, and the same program read twice
 * prints the same lines in the same order.
 */

#include "camel/passes/test/oper_shape.h"

#include "camel/execute/pass/registration.h"
#include "camel/execute/shaped_operators.h"
#include "camel/runtime/graph.h"

#include <format>
#include <set>
#include <string>
#include <string_view>

namespace {

using namespace camel::runtime;

/// The word a reading prints for a shape. `shapeName` (shaped_operators.h) is a back end's dump and
/// spells the node kind it used to be ("CAST"); this names the operation, which is what the two
/// forms are being compared by.
const char *shapeText(camel::execute::OperShape shape) {
    switch (shape) {
    case camel::execute::OperShape::Conversion:
        return "conversion";
    case camel::execute::OperShape::Clone:
        return "clone";
    case camel::execute::OperShape::Construction:
        return "construction";
    case camel::execute::OperShape::Access:
        return "access";
    case camel::execute::OperShape::None:
        break;
    }
    return "none";
}

/// The kind's name as the reading prints it, or nullptr for a kind this pass does not read. The
/// name is the shape's own (`shapeName`), which is what the rest of the tree prints for the four:
/// the kind a node was built as and the operator a call names are the same four names.
std::string_view kindFormName(GCNodeKind kind) {
    const camel::execute::OperShape shape = camel::execute::shapeOfKind(kind);
    return shape == camel::execute::OperShape::None
               ? std::string_view{}
               : std::string_view{camel::execute::shapeName(shape)};
}

/// The form of an OPER node, when it calls one of the four operations.
std::string operFormOf(const GCGraph &graph, gc_node_ref_t ref, const GCNode *node) {
    if (!node->hasPayload()) {
        return {};
    }
    const auto *body = graph.nodeBodyAs<GCOperBody>(ref);
    if (body == nullptr) {
        return {};
    }
    const std::string_view uri = body->uri();
    if (camel::execute::shapeOfUri(uri) == camel::execute::OperShape::None) {
        return {};
    }
    return std::format("oper={}", uri);
}

} // namespace

const camel::runtime::Program *
OperShapeTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    std::set<std::string> lines;
    for (auto *graph : program->graphs()) {
        if (!graph->hasNodePayload()) {
            continue;
        }
        for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
            const GCNode *node = *it;
            if (node == nullptr) {
                continue;
            }
            const gc_node_ref_t ref = it.ref();
            std::string form;
            if (const std::string_view kindForm = kindFormName(node->kind); !kindForm.empty()) {
                form = std::format("kind={}", kindForm);
            } else if (node->kind == GCNodeKind::Oper) {
                form = operFormOf(*graph, ref, node);
            }
            if (form.empty()) {
                continue;
            }
            lines.insert(
                std::format(
                    "[OPER_SHAPE] {} shape={}",
                    form,
                    shapeText(camel::execute::shapeOfOperation(*graph, ref))));
        }
    }
    for (const std::string &line : lines) {
        os << line << "\n";
    }
    return program;
}

namespace {

using camel::execute::makePass;
using camel::execute::PassKernel;

/// The test-only passes this file implements. Each name is the constant its own header
/// declares, so the name a pipeline writes and the class that answers it are one fact.
const PassKernel operShapeKernel(kOperShapeTestPass, makePass<OperShapeTestPass>());

} // namespace
