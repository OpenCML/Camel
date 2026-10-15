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
 * Created: Oct. 15, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * TEST-ONLY checks of the index semantics core (see index_semantics.h).
 */

#include "index_semantics.h"

#include "camel/core/operator_table.h"

#include <format>
#include <ostream>
#include <stdexcept>

namespace {

using camel::core::OperatorTable;
using namespace camel::core::sem;
using camel::core::type::TypeCode;

/// The text of `e` with dims d0, d1, ... and parameters p0, p1, ...
std::string show(const ExprPool &pool, ExprId e) { return pool.text(e, {}, {}); }

/// Runs `build` and prints what it built, or the exception it threw.
template <typename Fn> void attempt(std::ostream &os, std::string_view check, Fn &&build) {
    try {
        const std::string built = build();
        os << "INDEX " << check << " " << built << "\n";
    } catch (const std::exception &e) {
        os << "INDEX " << check << " rejected: " << e.what() << "\n";
    }
}

/**
 * The instance of a matrix product of A: [M, K] and B: [K, N] (design 5.3), with M, N, K as
 * extent parameters of known values: domain (m, n, k), A (m, k), B (k, n), a combining write.
 */
IndexedInstance matmulInstance(int64_t M, int64_t N, int64_t K) {
    IndexedInstance inst;
    ExprPool &x  = inst.exprs;
    auto &params = inst.domain.params;
    params       = {
        Param{.name = "M", .source = ParamSource::Extent, .operand = 0, .axis = 0, .value = M},
        Param{.name = "K", .source = ParamSource::Extent, .operand = 0, .axis = 1, .value = K},
        Param{.name = "N", .source = ParamSource::Extent, .operand = 1, .axis = 1, .value = N},
    };
    const ExprId m = x.dim(0), n = x.dim(1), k = x.dim(2);
    inst.domain.dims = {
        {"m", x.param(0), DimRole::Parallel},
        {"n", x.param(2), DimRole::Parallel},
        {"k", x.param(1), DimRole::Reduction},
    };
    inst.norm = {
        Access{.kind = AccessKind::Affine, .coords = {m, k}},
        Access{.kind = AccessKind::Affine, .coords = {k, n}},
    };
    inst.output = Write{.coords = {m, n}, .mode = WriteMode::Combine};
    inst.law    = 0;
    return inst;
}

} // namespace

camel::runtime::GCGraph *
IndexSemanticsTestPass::apply(camel::runtime::GCGraph *graph, std::ostream &os) {
    // ---- Expressions: simplification, interning, text, evaluation. ----
    {
        ExprPool x;
        const ExprId d0 = x.dim(0), d1 = x.dim(1), p0 = x.param(0);
        os << "INDEX fold " << show(x, x.add(x.constant(2), x.constant(3))) << "\n";
        os << "INDEX add_zero " << show(x, x.add(d0, x.constant(0))) << "\n";
        os << "INDEX constant_right " << show(x, x.add(x.constant(1), d0)) << "\n";
        os << "INDEX constants_merge " << show(x, x.add(x.add(d0, x.constant(2)), x.constant(-5)))
           << "\n";
        os << "INDEX subtract " << show(x, x.sub(d0, d1)) << "\n";
        os << "INDEX scale_nested " << show(x, x.scale(3, x.scale(4, d0))) << "\n";
        os << "INDEX param_factor " << show(x, x.mul(d0, p0)) << "\n";
        os << "INDEX const_factor_first " << show(x, x.mul(p0, x.constant(2))) << "\n";
        os << "INDEX div_mod "
           << show(x, x.mod(x.floorDiv(x.add(d0, d1), x.constant(4)), x.constant(3))) << "\n";
        os << "INDEX div_by_one " << show(x, x.floorDiv(d0, x.constant(1))) << " "
           << show(x, x.mod(d0, x.constant(1))) << "\n";
        os << "INDEX interned "
           << (x.add(x.scale(2, d0), d1) == x.add(x.scale(2, d0), d1) ? "yes" : "no") << "\n";
        // Mathematical floor division and modulo: -7 floordiv 2 = -4, -7 mod 2 = 1.
        os << "INDEX floor_negative " << show(x, x.floorDiv(x.constant(-7), x.constant(2))) << " "
           << show(x, x.mod(x.constant(-7), x.constant(2))) << "\n";
        const ExprId e                   = x.add(x.mul(p0, d0), x.mod(d1, x.constant(3)));
        const std::vector<int64_t> point = {4, 7};
        const std::vector<std::optional<int64_t>> values = {5};
        os << "INDEX evaluate " << x.evaluate(e, point, values) << "\n";
        const auto form = x.affine(x.add(x.mul(p0, d0), x.scale(-2, d1)), 2, values);
        os << "INDEX affine "
           << (form ? std::format("{} {} {}", form->constant, form->coeffs[0], form->coeffs[1])
                    : "none")
           << "\n";
        os << "INDEX affine_mod " << (x.affine(e, 2, values) ? "some" : "none") << "\n";
        const std::vector<std::optional<int64_t>> unknown = {std::nullopt};
        os << "INDEX affine_unknown " << (x.affine(e, 2, unknown) ? "some" : "none") << "\n";
        attempt(os, "product_of_dims", [&] { return show(x, x.mul(d0, d1)); });
        attempt(os, "mod_by_zero", [&] { return show(x, x.mod(d0, x.constant(0))); });
        attempt(os, "div_by_dim", [&] { return show(x, x.floorDiv(d0, d1)); });
        attempt(os, "overflow", [&] { return show(x, x.scale(INT64_MAX, x.constant(2))); });
    }

    // Exercise nested constant folding across repeated pool reallocations.
    {
        ExprPool x;
        const auto d                     = x.dim(0);
        const std::vector<int64_t> point = {7};
        for (int64_t factor = 2; factor < 4096; ++factor) {
            const auto nested = x.scale(factor, x.scale(3, d));
            if (x.evaluate(nested, point, {}) != factor * 21) {
                throw std::runtime_error("nested scale changed while the expression pool grew");
            }
        }
        os << "INDEX scale_pool_growth ok\n";
    }

    // ---- Validation. ----
    {
        auto check = [&](std::string_view name, const IndexedInstance &inst) {
            const auto problem = validate(inst);
            os << "INDEX validate_" << name << " " << (problem ? "rejected: " + *problem : "ok")
               << "\n";
        };
        check("matmul", matmulInstance(2, 3, 4));
        IndexedInstance noLaw = matmulInstance(2, 3, 4);
        noLaw.law             = kNoLaw;
        check("combine_without_law", noLaw);
        IndexedInstance unique = matmulInstance(2, 3, 4);
        unique.output.mode     = WriteMode::Unique;
        check("reduction_written_unique", unique);
        IndexedInstance dimExtent       = matmulInstance(2, 3, 4);
        dimExtent.domain.dims[1].extent = dimExtent.exprs.dim(0);
        check("extent_mentions_dim", dimExtent);
        IndexedInstance badParam = matmulInstance(2, 3, 4);
        badParam.domain.params.push_back(
            Param{.name = "s", .source = ParamSource::Runtime, .operand = 2});
        badParam.norm[0].coords[1] =
            badParam.exprs.floorDiv(badParam.exprs.dim(2), badParam.exprs.param(3));
        check("divisor_not_known_positive", badParam);
        badParam.domain.constraints.push_back(
            {badParam.exprs.constant(1), Constraint::Rel::Le, badParam.exprs.param(3)});
        check("divisor_required_positive", badParam);
        IndexedInstance outOfRange   = matmulInstance(2, 3, 4);
        outOfRange.norm[1].coords[0] = 999;
        check("expression_out_of_range", outOfRange);
    }

    // ---- The query through the operator table. ----
    {
        OperatorTable &table = OperatorTable::instance();
        auto declare         = [&](std::string_view uri, InstantiateFn fn) {
            table.declareSemantics(
                uri,
                OperatorSemantics{.key = OperatorKey::of(uri), .indexing = std::move(fn)});
        };
        declare("semtest:matmul", [](const InstanceContext &cx) -> std::optional<IndexedInstance> {
            if (cx.norm.size() != 2) {
                return std::nullopt;
            }
            return matmulInstance(2, 3, 4);
        });
        declare("semtest:throws", [](const InstanceContext &) -> std::optional<IndexedInstance> {
            throw std::runtime_error("a defect");
        });
        declare("semtest:invalid", [](const InstanceContext &) -> std::optional<IndexedInstance> {
            IndexedInstance inst = matmulInstance(2, 3, 4);
            inst.law             = kNoLaw;
            return inst;
        });
        camel::core::type::Type *const two[] = {
            camel::core::type::Type::Int64(),
            camel::core::type::Type::Int64()};
        const InstanceContext cx{.norm = two};
        const InstanceContext one{.norm = std::span(two, 1)};
        auto query = [&](std::string_view uri, const InstanceContext &c) {
            const auto inst = instantiate(table.intern(uri), c);
            return inst ? std::string("instance") : std::string("none");
        };
        os << "INDEX query_declared " << query("semtest:matmul", cx) << "\n";
        os << "INDEX query_no_instance " << query("semtest:matmul", one) << "\n";
        os << "INDEX query_throws " << query("semtest:throws", cx) << "\n";
        os << "INDEX query_invalid " << query("semtest:invalid", cx) << "\n";
        os << "INDEX query_undeclared " << query("semtest:nothing", cx) << "\n";
        if (const auto inst = instantiate(table.intern("semtest:matmul"), cx)) {
            os << "INDEX text\n" << toString(*inst);
        }
    }

    // ---- Splitting dims for tiles (design 5.2, example 5). ----
    {
        const IndexedInstance inst  = matmulInstance(5, 3, 7);
        const DimSplit splits[]     = {{0, 4}, {2, 4}};
        const IndexedInstance tiled = splitDims(inst, splits);
        os << "INDEX split\n" << toString(tiled);
        os << "INDEX split_valid " << (validate(tiled) ? "no" : "yes") << "\n";
        const auto extents = tiled.extents();
        std::string text;
        for (int64_t e : extents.value_or(std::vector<int64_t>{})) {
            text += std::format("{} ", e);
        }
        os << "INDEX split_extents " << text << "\n";
        // Count the points of the tiled box inside the guard: exactly the original M*N*K.
        size_t inside = 0;
        std::vector<int64_t> p(5, 0);
        for (p[0] = 0; p[0] < (*extents)[0]; ++p[0])
            for (p[1] = 0; p[1] < 4; ++p[1])
                for (p[2] = 0; p[2] < 3; ++p[2])
                    for (p[3] = 0; p[3] < (*extents)[3]; ++p[3])
                        for (p[4] = 0; p[4] < 4; ++p[4])
                            inside += holds(tiled, tiled.domain.guard, p) ? 1 : 0;
        os << "INDEX split_points " << inside << "\n";
        attempt(os, "split_bad_dim", [&] {
            const DimSplit bad[] = {{9, 2}};
            return toString(splitDims(inst, bad));
        });
    }

    // ---- The strided view of a row-major operand (Tiles' reindex). ----
    {
        // transpose of [3, 5]: domain (i, j) over [5, 3], x read at (j, i).
        IndexedInstance t;
        ExprPool &x   = t.exprs;
        t.domain.dims = {
            {"i", x.constant(5), DimRole::Parallel},
            {"j", x.constant(3), DimRole::Parallel}};
        t.norm   = {Access{.kind = AccessKind::Affine, .coords = {x.dim(1), x.dim(0)}}};
        t.output = Write{.coords = {x.dim(0), x.dim(1)}};
        const int64_t shape[] = {3, 5};
        auto view             = [&](const Access &a, std::span<const int64_t> s) {
            const auto v = rowMajorView(t, a, s);
            if (!v) {
                return std::string("none");
            }
            std::string out = std::format("offset {} strides", v->offset);
            for (int64_t st : v->strides) {
                out += std::format(" {}", st);
            }
            return out;
        };
        os << "INDEX view_transpose " << view(t.norm[0], shape) << "\n";
        // slice with start 1, step 2 on axis 1 of [3, 5]: (i, 1 + 2*j).
        const Access sliced{
            .kind   = AccessKind::Affine,
            .coords = {x.dim(1), x.add(x.constant(1), x.scale(2, x.dim(0)))}};
        os << "INDEX view_slice " << view(sliced, shape) << "\n";
        const Access reshaped{
            .kind   = AccessKind::Affine,
            .coords = {x.floorDiv(x.dim(0), x.constant(5)), x.mod(x.dim(0), x.constant(5))}};
        os << "INDEX view_reshape " << view(reshaped, shape) << "\n";
    }
    return graph;
}
