// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The pass of oper_shape.h: one line per form the program uses, in sorted order, and nothing else
 * about the program.
 *
 * The reading is a *set* rather than a count, so a graph this pass does not own (a library's, or a
 * node an unrelated edit adds) contributes no line of its own, and the same program read twice
 * prints the same lines in the same order.
 *
 * Before those lines it prints the shape registry's own answers (`[SHAPE_REGISTRY]`), once: the
 * statement of a shape, the shape read back off the record, and the refusal a completed record
 * gives a second, different statement. That half is about the registry and not about the program,
 * so it reads the same for every case, and it is here because the registry's only reader in the
 * tree is this question ("which of the four does this node perform").
 */

#include "camel/passes/test/oper_shape.h"

#include "camel/execute/pass/registration.h"
#include "camel/execute/shaped_operators.h"
#include "camel/runtime/graph.h"

#include <format>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using namespace camel::runtime;

/// The word a reading prints for a shape. `shapeName` (shaped_operators.h) is a back end's dump and
/// spells the node kind ("CAST"); this names the operation, which is what the two forms are being
/// compared by.
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

/// The form of an OPER node, when it calls one of the four operations. The shape is the one the
/// operator's record states, asked by the operator the node names: a name table here would be a
/// second reading of the registrations that state the shapes.
std::string operFormOf(const GCGraph &graph, gc_node_ref_t ref, const GCNode *node) {
    if (!node->hasPayload()) {
        return {};
    }
    const auto *body = graph.nodeBodyAs<GCOperBody>(ref);
    if (body == nullptr) {
        return {};
    }
    if (camel::runtime::shapeOfOperator(body->oper) == camel::execute::OperShape::None) {
        return {};
    }
    return std::format("oper={}", body->uri());
}

/// The shape registry's own answers, printed once per run: `declareShape` states a shape at a URI,
/// `shapeOfOperator` reads it back off the record, and a record that is completed refuses a second,
/// different statement and keeps the one it published. The URI is this test's own, because a test
/// may state a shape only where nothing a program can call is affected: a shape stated at one of the
/// four would change what every later reader of a graph is told about it.
void checkShapeRegistry(std::ostream &os) {
    using camel::core::OperId;
    using camel::core::OperatorTable;
    using camel::ops::OperShape;
    using camel::ops::Registry;

    OperatorTable &table = OperatorTable::instance();
    const auto report    = [&os](std::string_view what, bool ok) {
        os << "[SHAPE_REGISTRY] " << what << ' ' << (ok ? "ok" : "not ok") << "\n";
    };

    constexpr std::string_view uri = ":test/shape_registry";
    const OperId oper              = table.intern(uri);
    // A record nobody stated a shape at answers None, and so does an operator that is no operator:
    // the four are stated where they are registered, and the default is the statement's absence.
    report(
        "unstated_is_none",
        camel::runtime::shapeOfOperator(oper) == OperShape::None &&
            camel::runtime::shapeOfOperator(camel::core::kNoOper) == OperShape::None);
    // Before the record is completed a later statement stands (the bare-value convention of the
    // registration API), and the reader of the table's id and the record's own id answer together.
    table.declareShape(uri, OperShape::Construction);
    const bool stated = camel::runtime::shapeOfOperator(oper) == OperShape::Construction;
    table.declareShape(uri, OperShape::Clone);
    const camel::ops::Record *record = table.recordOf(oper);
    report(
        "declareShape",
        stated && record != nullptr && record->shape == OperShape::Clone &&
            camel::runtime::shapeOfOperator(oper) == OperShape::Clone);
    // A completed record is never changed again: the statement it already holds is a repetition,
    // and a different one is refused with both shapes named.
    Registry &registry = Registry::instance();
    registry.complete(registry.intern(uri));
    bool repetition = true;
    try {
        table.declareShape(uri, OperShape::Clone);
    } catch (const std::exception &) {
        repetition = false;
    }
    std::string refusal;
    try {
        table.declareShape(uri, OperShape::Access);
    } catch (const std::invalid_argument &error) {
        refusal = error.what();
    }
    report(
        "completed_refuses_a_second_shape",
        repetition && refusal.find("is completed as \"COPY\"") != std::string::npos &&
            refusal.find("says \"ACCS\"") != std::string::npos &&
            camel::runtime::shapeOfOperator(oper) == OperShape::Clone);
}

} // namespace

const camel::runtime::Program *
OperShapeTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    checkShapeRegistry(os);
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
