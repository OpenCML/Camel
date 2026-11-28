// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Three of the four core kinds, asked at one node of one graph: the shape the node produces
 * (`core.shape`), what the program granted there (`core.permissions`), and the index instance its
 * operator declares (`core.instance`).
 *
 * Each kind has one computation, in the framework's own registration (`camel/facts/core_kinds.h`),
 * and a reader that holds a graph should not have to name the store, the kind and the anchor to
 * reach it. These three functions are that name: one question, one call, and one place that says
 * what "no answer" is for each of them. The fourth kind, `core.arrangeable`, has had its reader
 * since it moved (`nodeIsFreelyArrangeable`, `camel/facts/arrangeable.h`) and this header follows
 * it.
 *
 * They read, they do not compute. The store's computation stays the only body, so a reader cannot
 * answer differently from `camel inspect --facts` about the same node, and the two stores' answers
 * cannot drift apart through a second copy of one of them.
 *
 * The store that answers is the one that holds `graph` (`storeHolding`, `camel/facts/facts.h`):
 * a graph no store holds is one the framework has nothing to say about, and every reader here
 * answers its "no answer" rather than inventing one.
 */

#pragma once

#include "camel/facts/core_kinds.h"

#include "camel/runtime/core_facts.h"

#include <optional>

namespace camel::facts {

/// The shape `core.shape` gives the value `node` produces. Nothing when no store holds the graph,
/// when the graph's own face has no such node (a JOIN, a reference to a place that was removed --
/// a caller's mistake, which no fact answers), or when the store answers nothing there.
std::optional<camel::runtime::fact::TensorShape> shapeOf(const graph::Graph &graph, NodeId node);

/// The numeric permissions granted at `node`, which is what `core.permissions` holds there.
///
/// `0` means "nothing granted", the value the accessor this replaces already produced
/// (`graph::facts`, `graph.permissions`): at this call "nothing granted" and "granted nothing"
/// are one number, which is why the kind itself keeps them apart and this reader does not.
int permissionsOf(const graph::Graph &graph, NodeId node);

/// The index instance of the operator `node` runs, with the facts at that node, which is what
/// `core.instance` holds there. Nothing when no store holds the graph, when the node has no such
/// fact -- the operator declares no index semantics, or the node is not one the graph has -- or
/// when the store answers nothing there.
std::optional<camel::runtime::fact::NodeInstance>
instanceAt(const graph::Graph &graph, NodeId node);

} // namespace camel::facts