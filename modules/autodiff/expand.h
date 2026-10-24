// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The compile-time gradient expansion: `autodiff::expand` (docs/design/gpu/plan.cn.md 2.6).
 */

#pragma once

namespace camel::autodiff {

/// Registers the pass `autodiff::expand`. `tiles::compile` applies it to the graphs of a `#gpu`
/// scope before it compiles them (docs/design/gpu/plan-modules.cn.md 5, decision 2), so a user
/// never writes it; it stays an ordinary pass for a pipeline that wants the derived graph built
/// at compile time on its own.
void registerExpandPass();

} // namespace camel::autodiff