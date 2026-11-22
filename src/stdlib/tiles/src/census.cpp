// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The operator surface of a program, grouped by what the device can run (`tiles::census`).

#include "camel/core/operator_table.h"
#include "camel/execute/pass/opt.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/reachable.h"
#include "tiles/internal/regions.h"

#include <memory>
#include <ostream>
#include <set>
#include <string>
#include <string_view>

namespace camel::tiles {
namespace {
namespace rt = runtime;

/// Whether the operator is one the device could ever run, read from the module that provides it:
/// `tensor` holds the arrays the device computes and `nn` holds the parameter trees it updates, so
/// those two are the surface a `#gpu` scope asks the device for. An operator of another module --
/// the core's own primitives, and the strings, bytes and scalars of the other modules -- is not on
/// it: those run where the interpreter runs, and counting them would report a gap no device program
/// can close.
bool onSurface(std::string_view uri) {
    const std::string_view module = uri.substr(0, uri.find(':'));
    return module == "tensor" || module == "nn";
}

/// The distinct operators of the surface that the graphs of a program hold, each in one of the two
/// groups the capability table decides: the device's (the table names it, and the region compiler
/// lowers it) or a gap (no module offers a device program for it, which a `#gpu` scope refuses).
///
/// Two kinds of surface operator are in neither group, as in the region compiler
/// (`modules/tiles/src/region_outline.cpp`): a macro operator (`grad`, `with_grad`) is a pass's
/// business (`std::macro`), never an operator the device cannot run, and an operator with an
/// external effect is the boundary of a device scope rather than a gap.
struct OperatorSurface {
    std::set<std::string> device;
    std::set<std::string> gap;

    size_t operators() const { return device.size() + gap.size(); }
};

OperatorSurface operatorSurface(const rt::Program &program) {
    OperatorSurface surface;
    auto &table = core::OperatorTable::instance();
    for (const rt::GCGraph *graph : rt::collectReachableGraphs(program, program.root())) {
        for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
            const auto ref   = it.ref();
            const auto *node = *it;
            if (node->kind != rt::GCNodeKind::Oper)
                continue;
            const auto oper        = graph->nodeBodyAs<rt::GCOperBody>(ref)->oper;
            const std::string name(table.uri(oper));
            if (!onSurface(name) || node->isMacro())
                continue;
            if (!table.isPure(oper) || table.hasExternalEffect(oper))
                continue;
            (deviceCapable(oper) ? surface.device : surface.gap).insert(name);
        }
    }
    return surface;
}

/// One line of counts, then one line per operator of each group, so what the surface holds is a
/// list of names as well as a number: the device's, then the gap's. Each is sorted, so two runs of
/// the same program print the same lines.
std::string censusReport(const rt::Program &program) {
    const OperatorSurface surface = operatorSurface(program);
    std::string report            = "TILES_CENSUS ops=" + std::to_string(surface.operators()) +
                         " device=" + std::to_string(surface.device.size()) +
                         " gap=" + std::to_string(surface.gap.size()) + '\n';
    for (const std::string &name : surface.device)
        report += "TILES_CENSUS_DEVICE " + name + '\n';
    for (const std::string &name : surface.gap)
        report += "TILES_CENSUS_GAP " + name + '\n';
    return report;
}

/// Prints the operator surface and consumes the program: the census is a reading of the program as
/// the pipeline wrote it, so nothing after it runs.
class CensusPass final : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const rt::Program *apply(const rt::Program *program, std::ostream &os) override {
        os << censusReport(*program);
        return nullptr;
    }
};
} // namespace

void registerCensusPass() {
    registerModulePass("tiles::census", [](const core::context::context_ptr_t &context) {
        return std::make_unique<CensusPass>(context);
    });
}
} // namespace camel::tiles