// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "builtin.h"
#include "camel/core/context/frame.h"
#include "camel/core/error/diagnostics.h"
#include "camel/execute/fault_isolation.h"
#include "camel/utils/log.h"

using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::rtdata;

#include "../operators/array_convert.h"
#include "../operators/cast.h"
#include "../operators/collection.h"
#include "../operators/enum.h"
#include "../operators/half_ops.h"
#include "../operators/higher_order.h"
#include "../operators/io.h"
#include "../operators/macro.h"
#include "../operators/mem.h"
#include "../operators/ops.h"
#include "../operators/profiler.h"
#include "../operators/set_value.h"
#include "../operators/str.h"
#include "../operators/stream.h"
#include "../operators/struct.h"
#include "../operators/tagged.h"
#include "camel/core/operator_traits.h"

#include <string_view>
#include <unordered_set>

namespace {

using camel::core::type::TypeCode;

/// A conversion of the language: an operator that reads a `source` and writes a `target`.
///
/// The two types are written here, beside the operator's implementation, because they are
/// what the operator *is*. The registration hands them to the operator table
/// (`OperatorTable::declareConversion`), so a reader asks there whether an operator converts
/// and between which types, instead of decoding the letters of its URI: a conversion's rule
/// is filed under its destination's spelling (`src/core/forward.cpp#conversionKeyOf`), and a
/// variable read whose result is a float is not a conversion.
struct ConversionRegistration {
    const char *name; // the URI in the builtin namespace: `op/itod`
    operator_t impl;
    TypeCode source;
    TypeCode target;
};

constexpr ConversionRegistration kConversions[] = {
    // A type name applied to a numeric array: the conversion of every element on its own.
    {"op/atoi", __atoi__, TypeCode::Array, TypeCode::Int32},
    {"op/atol", __atol__, TypeCode::Array, TypeCode::Int64},
    {"op/atof", __atof__, TypeCode::Array, TypeCode::Float32},
    {"op/atod", __atod__, TypeCode::Array, TypeCode::Float64},
    {"op/atobf", __atobf__, TypeCode::Array, TypeCode::BFloat16},
    {"op/atof6", __atof6__, TypeCode::Array, TypeCode::Float16},

    {"op/itoi", __itoi__, TypeCode::Int32, TypeCode::Int32},
    {"op/ltoi", __ltoi__, TypeCode::Int64, TypeCode::Int32},
    {"op/ftoi", __ftoi__, TypeCode::Float32, TypeCode::Int32},
    {"op/dtoi", __dtoi__, TypeCode::Float64, TypeCode::Int32},
    {"op/stoi", __stoi__, TypeCode::String, TypeCode::Int32},

    {"op/itol", __itol__, TypeCode::Int32, TypeCode::Int64},
    {"op/ltol", __ltol__, TypeCode::Int64, TypeCode::Int64},
    {"op/ftol", __ftol__, TypeCode::Float32, TypeCode::Int64},
    {"op/dtol", __dtol__, TypeCode::Float64, TypeCode::Int64},
    {"op/stol", __stol__, TypeCode::String, TypeCode::Int64},

    {"op/itof", __itof__, TypeCode::Int32, TypeCode::Float32},
    {"op/ltof", __ltof__, TypeCode::Int64, TypeCode::Float32},
    {"op/ftof", __ftof__, TypeCode::Float32, TypeCode::Float32},
    {"op/dtof", __dtof__, TypeCode::Float64, TypeCode::Float32},
    {"op/stof", __stof__, TypeCode::String, TypeCode::Float32},

    {"op/itod", __itod__, TypeCode::Int32, TypeCode::Float64},
    {"op/ltod", __ltod__, TypeCode::Int64, TypeCode::Float64},
    {"op/ftod", __ftod__, TypeCode::Float32, TypeCode::Float64},
    {"op/dtod", __dtod__, TypeCode::Float64, TypeCode::Float64},
    {"op/stod", __stod__, TypeCode::String, TypeCode::Float64},

    {"op/itoc", __itoc__, TypeCode::Int32, TypeCode::Complex64},
    {"op/ltoc", __ltoc__, TypeCode::Int64, TypeCode::Complex64},
    {"op/ftoc", __ftoc__, TypeCode::Float32, TypeCode::Complex64},
    {"op/dtoc", __dtoc__, TypeCode::Float64, TypeCode::Complex64},
    {"op/ctoc", __ctoc__, TypeCode::Complex64, TypeCode::Complex64},
    {"op/ztoc", __ztoc__, TypeCode::Complex128, TypeCode::Complex64},

    {"op/itoz", __itoz__, TypeCode::Int32, TypeCode::Complex128},
    {"op/ltoz", __ltoz__, TypeCode::Int64, TypeCode::Complex128},
    {"op/ftoz", __ftoz__, TypeCode::Float32, TypeCode::Complex128},
    {"op/dtoz", __dtoz__, TypeCode::Float64, TypeCode::Complex128},
    {"op/ctoz", __ctoz__, TypeCode::Complex64, TypeCode::Complex128},
    {"op/ztoz", __ztoz__, TypeCode::Complex128, TypeCode::Complex128},

    // bf16 and f16 are floating types of the language with a written conversion of their
    // own (design principles 7.3): `bf16(x)`, `f16(x)` and `x as bf16` round to nearest with
    // ties to even once and need no numeric permission. `bf` is bf16 and `f6` is f16.
    {"op/itobf", __itobf__, TypeCode::Int32, TypeCode::BFloat16},
    {"op/ltobf", __ltobf__, TypeCode::Int64, TypeCode::BFloat16},
    {"op/ftobf", __ftobf__, TypeCode::Float32, TypeCode::BFloat16},
    {"op/dtobf", __dtobf__, TypeCode::Float64, TypeCode::BFloat16},
    {"op/bftobf", __bftobf__, TypeCode::BFloat16, TypeCode::BFloat16},
    {"op/stobf", __stobf__, TypeCode::String, TypeCode::BFloat16},

    {"op/itof6", __itof6__, TypeCode::Int32, TypeCode::Float16},
    {"op/ltof6", __ltof6__, TypeCode::Int64, TypeCode::Float16},
    {"op/ftof6", __ftof6__, TypeCode::Float32, TypeCode::Float16},
    {"op/dtof6", __dtof6__, TypeCode::Float64, TypeCode::Float16},
    {"op/f6tof6", __f6tof6__, TypeCode::Float16, TypeCode::Float16},
    {"op/stof6", __stof6__, TypeCode::String, TypeCode::Float16},

    {"op/bftoi", __bftoi__, TypeCode::BFloat16, TypeCode::Int32},
    {"op/bftol", __bftol__, TypeCode::BFloat16, TypeCode::Int64},
    {"op/bftof", __bftof__, TypeCode::BFloat16, TypeCode::Float32},
    {"op/bftod", __bftod__, TypeCode::BFloat16, TypeCode::Float64},

    {"op/f6toi", __f6toi__, TypeCode::Float16, TypeCode::Int32},
    {"op/f6tol", __f6tol__, TypeCode::Float16, TypeCode::Int64},
    {"op/f6tof", __f6tof__, TypeCode::Float16, TypeCode::Float32},
    {"op/f6tod", __f6tod__, TypeCode::Float16, TypeCode::Float64},

    {"op/atos", __atos__, TypeCode::Array, TypeCode::String},
};

/// The operators the builtin executor implements, by URI in the builtin namespace. The
/// conversions are added from kConversions, which is also where their types are written.
std::unordered_map<std::string, operator_t> makeOpsImplMap() {
    std::unordered_map<std::string, operator_t> map = {
        // The scalar operations of the two reduced-precision floating types (design principles
        // 7.3): each widens its operands to f32, computes there and rounds the result once into
        // the element type, which is what a bf16 or f16 value is a member of. `bf` is bf16 and
        // `f6` is f16.
        {"op/eq_bf", __builtin__eq__},
        {"op/eq_f6", __builtin__eq__},
        {"op/lt_bf", __builtin__lt_bf__},
        {"op/le_bf", __builtin__le_bf__},
        {"op/gt_bf", __builtin__gt_bf__},
        {"op/ge_bf", __builtin__ge_bf__},
        {"op/lt_f6", __builtin__lt_f6__},
        {"op/le_f6", __builtin__le_f6__},
        {"op/gt_f6", __builtin__gt_f6__},
        {"op/ge_f6", __builtin__ge_f6__},
        {"op/add_bf", __builtin__add_bfbf__},
        {"op/sub_bf", __builtin__sub_bfbf__},
        {"op/mul_bf", __builtin__mul_bfbf__},
        {"op/div_bf", __builtin__div_bfbf__},
        {"op/pow_bf", __builtin__pow_bfbf__},
        {"op/neg_bf", __builtin__neg_bf__},
        {"op/add_f6", __builtin__add_f6f6__},
        {"op/sub_f6", __builtin__sub_f6f6__},
        {"op/mul_f6", __builtin__mul_f6f6__},
        {"op/div_f6", __builtin__div_f6f6__},
        {"op/pow_f6", __builtin__pow_f6f6__},
        {"op/neg_f6", __builtin__neg_f6__},
        {"op/assn_add_bf", __builtin__assn_add_bf__},
        {"op/assn_sub_bf", __builtin__assn_sub_bf__},
        {"op/assn_mul_bf", __builtin__assn_mul_bf__},
        {"op/assn_div_bf", __builtin__assn_div_bf__},
        {"op/assn_pow_bf", __builtin__assn_pow_bf__},
        {"op/assn_add_f6", __builtin__assn_add_f6__},
        {"op/assn_sub_f6", __builtin__assn_sub_f6__},
        {"op/assn_mul_f6", __builtin__assn_mul_f6__},
        {"op/assn_div_f6", __builtin__assn_div_f6__},
        {"op/assn_pow_f6", __builtin__assn_pow_f6__},

        // diagnostics
        {"op/mem_blocks", __builtin__mem_blocks__},
        // `xs->append<v>` written by the loop-state rewrite; not a group a program can name.
        {"struct/append_arr_owned", __append_owned_arr__},
        // The in-place form of `append` (Builder::inPlaceRebindingOf); likewise internal.
        {"struct/append_arr_in_place", __append_in_place_arr__},
        // The private copy of an array a rewritten loop starts from; likewise internal.
        {"struct/copy_arr", __copy_arr__},

        // variables
        {"var/new", __builtin__var_new__},
        {"var/new/empty", __builtin__var_new_empty__},
        {"var/after", __builtin__var_after__},
        {"var/get", __builtin__var_get__},
        {"var/peek", __builtin__var_peek__},
        // `xs = xs->sort` as a statement (Builder::inPlaceRebindingOf); with :op/assn_owned
        // below, not groups a program can name.
        {"var/take", __builtin__var_take__},

        // ops
        {"op/assn_i", __builtin__assn__},
        {"op/assn_l", __builtin__assn__},
        {"op/assn_f", __builtin__assn__},
        {"op/assn_d", __builtin__assn__},
        {"op/assn_c", __builtin__assn__},
        {"op/assn_z", __builtin__assn__},
        {"op/assn_s", __builtin__assn__},
        {"op/assn_b", __builtin__assn__},
        {"op/assn", __builtin__assn__},
        // `acc = acc->append<v>` as a statement; not a group a program can name.
        {"op/assn_owned", __builtin__assn_owned__},

        // The `set` operators a path assignment lowers to (Builder::pathAssignmentOf) and the
        // in-place form of the element set, which only the compiler names.
        {"op/set_field", __builtin__set_field__},
        {"op/set_index", __builtin__set_index__},
        {"op/set_index_in_place", __builtin__set_index_in_place__},

        {"op/assn_add_i", __builtin__assn_add_i__},
        {"op/assn_add_l", __builtin__assn_add_l__},
        {"op/assn_add_f", __builtin__assn_add_f__},
        {"op/assn_add_d", __builtin__assn_add_d__},
        {"op/assn_add_c", __builtin__assn_add_c__},
        {"op/assn_add_z", __builtin__assn_add_z__},
        {"op/assn_add_s", __builtin__assn_add_s__},

        {"op/assn_sub_i", __builtin__assn_sub_i__},
        {"op/assn_sub_l", __builtin__assn_sub_l__},
        {"op/assn_sub_f", __builtin__assn_sub_f__},
        {"op/assn_sub_d", __builtin__assn_sub_d__},
        {"op/assn_sub_c", __builtin__assn_sub_c__},
        {"op/assn_sub_z", __builtin__assn_sub_z__},

        {"op/assn_mul_i", __builtin__assn_mul_i__},
        {"op/assn_mul_l", __builtin__assn_mul_l__},
        {"op/assn_mul_f", __builtin__assn_mul_f__},
        {"op/assn_mul_d", __builtin__assn_mul_d__},
        {"op/assn_mul_c", __builtin__assn_mul_c__},
        {"op/assn_mul_z", __builtin__assn_mul_z__},

        {"op/assn_div_i", __builtin__assn_div_i__},
        {"op/assn_div_l", __builtin__assn_div_l__},
        {"op/assn_div_f", __builtin__assn_div_f__},
        {"op/assn_div_d", __builtin__assn_div_d__},
        {"op/assn_div_c", __builtin__assn_div_c__},
        {"op/assn_div_z", __builtin__assn_div_z__},

        {"op/assn_mod_i", __builtin__assn_mod_i__},
        {"op/assn_mod_l", __builtin__assn_mod_l__},

        {"op/assn_pow_i", __builtin__assn_pow_i__},
        {"op/assn_pow_l", __builtin__assn_pow_l__},
        {"op/assn_pow_f", __builtin__assn_pow_f__},
        {"op/assn_pow_d", __builtin__assn_pow_d__},
        {"op/assn_pow_c", __builtin__assn_pow_c__},
        {"op/assn_pow_c_i", __builtin__assn_pow_c__},
        {"op/assn_pow_c_l", __builtin__assn_pow_c__},
        {"op/assn_pow_z", __builtin__assn_pow_z__},
        {"op/assn_pow_z_i", __builtin__assn_pow_z__},
        {"op/assn_pow_z_l", __builtin__assn_pow_z__},

        {"op/assn_and", __builtin__assn_and__},
        {"op/assn_or", __builtin__assn_or__},

        {"op/and", __builtin__and__},
        {"op/or", __builtin__or__},

        {"op/eq_i", __builtin__eq__},
        {"op/eq_l", __builtin__eq__},
        {"op/eq_f", __builtin__eq__},
        {"op/eq_d", __builtin__eq__},
        {"op/eq_b", __builtin__eq__},
        {"op/eq_s", __builtin__eq__},
        {"op/eq_c", __builtin__eq__},
        {"op/eq_z", __builtin__eq__},
        {"op/eq", __builtin__eq__},

        {"op/ne", __builtin__ne__},

        {"op/lt_i", __builtin__lt__<Int32>},
        {"op/lt_l", __builtin__lt__<Int64>},
        {"op/lt_f", __builtin__lt__<Float32>},
        {"op/lt_d", __builtin__lt__<Float64>},
        {"op/lt_s", __builtin__lt_s__},

        {"op/le_i", __builtin__le__<Int32>},
        {"op/le_l", __builtin__le__<Int64>},
        {"op/le_f", __builtin__le__<Float32>},
        {"op/le_d", __builtin__le__<Float64>},
        {"op/le_s", __builtin__le_s__},

        {"op/gt_i", __builtin__gt__<Int32>},
        {"op/gt_l", __builtin__gt__<Int64>},
        {"op/gt_f", __builtin__gt__<Float32>},
        {"op/gt_d", __builtin__gt__<Float64>},
        {"op/gt_s", __builtin__gt_s__},

        {"op/ge_i", __builtin__ge__<Int32>},
        {"op/ge_l", __builtin__ge__<Int64>},
        {"op/ge_f", __builtin__ge__<Float32>},
        {"op/ge_d", __builtin__ge__<Float64>},
        {"op/ge_s", __builtin__ge_s__},

        {"op/add_i", __builtin__add_ii__},
        {"op/add_l", __builtin__add_ll__},
        {"op/add_f", __builtin__add_ff__},
        {"op/add_d", __builtin__add_dd__},
        {"op/add_s", __builtin__add_ss__},
        {"op/add_c", __builtin__add_cc__},
        {"op/add_z", __builtin__add_zz__},

        {"op/sub_i", __builtin__sub_ii__},
        {"op/sub_l", __builtin__sub_ll__},
        {"op/sub_f", __builtin__sub_ff__},
        {"op/sub_d", __builtin__sub_dd__},
        {"op/sub_c", __builtin__sub_cc__},
        {"op/sub_z", __builtin__sub_zz__},

        {"op/mul_i", __builtin__mul__},
        {"op/mul_l", __builtin__mul__},
        {"op/mul_f", __builtin__mul__},
        {"op/mul_d", __builtin__mul__},
        {"op/mul_c", __builtin__mul_cc__},
        {"op/mul_z", __builtin__mul_zz__},
        {"op/mul", __builtin__mul__},

        {"op/div_i", __builtin__div__},
        {"op/div_l", __builtin__div__},
        {"op/div_f", __builtin__div__},
        {"op/div_d", __builtin__div__},
        {"op/div_c", __builtin__div_cc__},
        {"op/div_z", __builtin__div_zz__},
        {"op/div", __builtin__div__},

        {"op/mod_i", __builtin__mod__},
        {"op/mod_l", __builtin__mod__},
        {"op/mod", __builtin__mod__},

        {"op/pow_i", __builtin__pow__},
        {"op/pow_l", __builtin__pow__},
        {"op/pow_f", __builtin__pow__},
        {"op/pow_d", __builtin__pow__},
        {"op/pow_c", __builtin__pow_c__},
        {"op/pow_c_i", __builtin__pow_c__},
        {"op/pow_c_l", __builtin__pow_c__},
        {"op/pow_z", __builtin__pow_z__},
        {"op/pow_z_i", __builtin__pow_z__},
        {"op/pow_z_l", __builtin__pow_z__},
        {"op/pow", __builtin__pow__},

        {"op/idx_arr", __builtin__idx__},
        {"op/idx_str", __builtin__idx__},
        {"op/idx_take", __idx_take_arr__},
        {"op/slice_sub", __slice_sub__},
        {"op/slice_str", __slice_str__},
        {"op/slice_bytes", __slice_bytes__},
        {"struct/len_bytes", __len_bytes__},
        {"op/add_bytes", __add_bytes__},
        {"op/eq_bytes", __eq_bytes__},

        {"op/not", __builtin__not__},

        {"op/neg_i", __builtin__neg__},
        {"op/neg_l", __builtin__neg__},
        {"op/neg_f", __builtin__neg__},
        {"op/neg_d", __builtin__neg__},
        {"op/neg_c", __builtin__neg_c__},
        {"op/neg_z", __builtin__neg_z__},

        {"op/inv_i", __builtin__inv__},
        {"op/inv_l", __builtin__inv__},

        // IO (built-in input/print/println/eprintln)
        {"io/print", __op_print__},
        {"io/println", __op_println__},
        {"io/eprintln", __op_eprintln__},

        // string
        {"str/format", __format__},

        {"str/join", __join__},

        // struct
        {"struct/len_str", __len_str__},
        {"struct/iter_str", __iter_str__},
        {"struct/len_arr", __len_arr__},
        // The higher-order operators: the one implementation of what they mean, by the
        // function-call service. Schedulers that run a shape of call faster by the operator's
        // declaration (OperatorSemantics::elementCalls) do; the macro evaluator runs these.
        {"mark/map_arr", __map_arr__},
        {"mark/filter_arr", __filter_arr__},
        {"mark/reduce_arr", __reduce_arr__},
        {"mark/unordered_reduce_arr", __reduce_arr__},
        {"mark/foreach_arr", __foreach_arr__},
        {"mark/unordered_foreach_arr", __foreach_arr__},

        {"struct/zip", __zip__},

        {"struct/last_arr", __last_arr__},


        {"struct/range", __range__},


        {"struct/concat_arr", __concat_arr__},

        {"struct/append_arr", __append_arr__},


        {"struct/contains_arr", __contains_arr__},
        {"struct/contains_str", __contains_str__},

        // Tangent arrays, inserted by autodiff into gradient graphs, never written by a program.
        {"tangent/add_arr", __tangent_add_arr__},
        {"tangent/zeros_arr", __tangent_zeros_arr__},
        {"tangent/scatter_add_arr", __tangent_scatter_add_arr__},
        {"tangent/scatter_slice_arr", __tangent_scatter_slice_arr__},
        {"tangent/scatter_take_arr", __tangent_scatter_take_arr__},
        {"tangent/init_arr", __tangent_init_arr__},
        {"tangent/last_arr", __tangent_last_arr__},

        // profiler
        {"profiler/begin", __profiler_begin__},
        {"profiler/end", __profiler_end__},
        {"profiler/instant", __profiler_instant__},
        {"profiler/enable", __profiler_enable__},

        // The bookkeeping of `tee` (operators/stream.h).
        {"stream/tee_note", __stream_tee_note__},

        // macro
        {"macro/cmp", __cmp__},

        // Tagged forms T? and T! (operators/tagged.h). All are under op/: pure.
        {"op/tag_is_null", __is_null__},
        {"op/tag_null_value", __null_value__},
        {"op/tag_is_error", __is_error__},
        {"op/tag_error_value", __error_value__},
        {"op/tag_error_info", __error_info__},
        {"op/tag_throw", __throw__},
        {"op/error_new", __error_new__},
        // The program's own premise check: it faults, so it is no pure operator.
        {"op/fail", __fail__},
        {"op/parse_int", __parse_int__},
        {"op/parse_float", __parse_float__},

        // The enum (operators/enum.h). All are under enum/: pure.
        {"enum/make", __enum_make__},
        {"enum/tag", __enum_tag__},
        {"enum/field", __enum_field__},

        // Map and Set (operators/collection.h). All are under coll/: pure.
        {"coll/map_idx", __coll_map_idx__},
        {"coll/map_get", __coll_map_get__},
        {"coll/map_get_or", __coll_map_get_or__},
        {"coll/has", __coll_has__},
        {"coll/set_contains", __coll_has__},
        {"coll/len", __coll_len__},
        {"coll/map_put", __coll_map_put__},
        {"coll/set_add", __coll_set_add__},
        {"coll/remove", __coll_remove__},
        {"coll/merge", __coll_merge__},
        {"coll/set_union", __coll_merge__},
        {"coll/set_intersect", __coll_set_intersect__},
        {"coll/set_difference", __coll_set_difference__},
        {"coll/set_is_subset", __coll_set_is_subset__},
        {"coll/keys", __coll_keys__},
        {"coll/map_values", __coll_map_values__},
        {"coll/map_items", __coll_map_items__},
        {"coll/set_items", __coll_keys__},
        {"coll/eq", __coll_eq__},
    };
    for (const ConversionRegistration &conversion : kConversions) {
        map.emplace(conversion.name, conversion.impl);
    }
    return map;
}

} // namespace

const std::unordered_map<std::string, operator_t> &getOpsImplMap() {
    static const std::unordered_map<std::string, operator_t> map = makeOpsImplMap();
    return map;
}

void registerBuiltinOperatorTraits() {
    // Pure: conversions, arithmetic, comparisons, logic, and indexing (everything under op/
    // except the assignment family, which writes a variable), plus the string and array
    // helpers, none of which changes its arguments: `append` and `extend` return a new array
    // (design principles 11.3). io/, profiler/ and input are effectful.
    static const std::unordered_set<std::string_view> kPureHelpers = {
        "str/format",
        "str/join",
        "struct/len_str",
        "struct/iter_str",
        "struct/len_arr",
        "struct/zip",
        "struct/last_arr",
        "op/idx_take",
        "op/slice_sub",
        "op/slice_str",
        "op/slice_bytes",
        "struct/len_bytes",
        "op/add_bytes",
        "op/eq_bytes",
        "struct/range",
        "struct/concat_arr",
        "struct/append_arr",
        "struct/contains_arr",
        "struct/contains_str",
        "tangent/add_arr",
        "tangent/zeros_arr",
        "tangent/scatter_add_arr",
        "tangent/scatter_slice_arr",
        "tangent/scatter_take_arr",
        "tangent/init_arr",
        "tangent/last_arr",
    };
    // Declared never to fault, where the implementation has no fault path at all: integer
    // addition and subtraction wrap, floating-point arithmetic and ordering cannot fail, and
    // the strict boolean operators only read their operands. Everything else keeps the
    // default (may fault). The passes do act on this list: `src/runtime/graph.cpp#nodeMayFault`
    // reads `mayFault` beside `elementwise` to answer design principles 3.4's predicate, and
    // `nodeIsFreelySchedulable` hands that answer to whoever may drop, move or recompute a node
    // -- an operator left out of this list is a running the program observes, so `std::opt`
    // keeps it alive with a `GATE` when a removal takes its last read
    // (`src/passes/opt/generic/rewrite.cpp#isObservedRun`). Each entry here is one operator of
    // docs/handover/reports/faults-table-audit.md, which reads its implementation against this
    // list.
    static const std::unordered_set<std::string_view> kNeverFaults = {
        "op/add_i",
        "op/add_l",
        "op/add_f",
        "op/add_d",
        "op/sub_i",
        "op/sub_l",
        "op/sub_f",
        "op/sub_d",
        "op/lt_i",
        "op/lt_l",
        "op/lt_f",
        "op/lt_d",
        "op/le_i",
        "op/le_l",
        "op/le_f",
        "op/le_d",
        "op/gt_i",
        "op/gt_l",
        "op/gt_f",
        "op/gt_d",
        "op/ge_i",
        "op/ge_l",
        "op/ge_f",
        "op/ge_d",
        "op/and",
        "op/or",
        "op/not",
        // Multiplication and negation wrap like addition does; equality tests a value of a type
        // that has one and string ordering reads two strings.
        "op/mul_i",
        "op/neg_i",
        "op/mul_l",
        "op/neg_l",
        "op/mul_f",
        "op/neg_f",
        "op/mul_d",
        "op/neg_d",
        "op/eq_i",
        "op/eq_l",
        "op/eq_f",
        "op/eq_d",
        "op/eq_b",
        "op/eq_s",
        "op/eq_c",
        "op/eq_z",
        "op/lt_s",
        "op/le_s",
        "op/gt_s",
        "op/ge_s",
        // The same declarations for the two reduced-precision floating types (design principles
        // 7.3): their arithmetic and ordering cannot fail, and `bf16(x) / y` keeps the fault of
        // division by zero that f32 has.
        "op/add_bf",
        "op/sub_bf",
        "op/mul_bf",
        "op/neg_bf",
        "op/pow_bf",
        "op/lt_bf",
        "op/le_bf",
        "op/gt_bf",
        "op/ge_bf",
        "op/add_f6",
        "op/sub_f6",
        "op/mul_f6",
        "op/neg_f6",
        "op/pow_f6",
        "op/lt_f6",
        "op/le_f6",
        "op/gt_f6",
        "op/ge_f6",
        // `pow` of the wide scalar types and of the complex ones is the library call of the same
        // name (ops.cpp#__builtin__pow__, ops.cpp#complexPowBinary): outside its domain it returns
        // inf or NaN, and a negative whole exponent is the reciprocal of an integer power, which
        // cannot fault either. The two reduced-precision entries above already say the same.
        "op/pow_i",
        "op/pow_l",
        "op/pow_f",
        "op/pow_d",
        "op/pow_c",
        "op/pow_c_i",
        "op/pow_c_l",
        "op/pow_z",
        "op/pow_z_i",
        "op/pow_z_l",
        // `len` reads a stored count or, for a string, counts its code points
        // (struct.cpp#__len_arr__, struct.cpp#__len_str__, collection.cpp#__coll_len__): there is
        // no fault path, and none of the three has numeric semantics of its own, so the trait is
        // where the declaration lives.
        "struct/len_arr",
        "struct/len_str",
        "coll/len",
        // The predicates over a collection: `has` and `contains` walk the entries looking for a
        // key, `eq` and `neq` compare two collections by content, and `set_is_subset` walks one
        // looking for each element of the other (collection.cpp#__coll_has__, #__coll_eq__,
        // #__coll_set_is_subset__). None of them builds a value, so none of them
        // can run out of memory either, which is the one fault the rest of `coll/` does have.
        "coll/has",
        "coll/set_contains",
        "coll/eq",
        "coll/set_is_subset",
        // The read-only helpers of `prelude.cml`: `contains` over an array walks it comparing
        // elements by content, `contains` over a string searches it, `len` of `Bytes` reads a
        // length and `==`/`!=` of two `Bytes` compare them (struct.cpp#__contains_arr__,
        // #__contains_str__, #__len_bytes__, #__eq_bytes__). None of them builds a
        // value, so a failed allocation is not among their paths either.
        "struct/contains_arr",
        "struct/contains_str",
        "struct/len_bytes",
        "op/eq_bytes",
    };
    // The conversions of an array apply to each element on its own; every other conversion reads
    // and writes one value. Which ones take an array is the source type kConversions states.
    std::unordered_set<std::string_view> arrayConversions;
    for (const ConversionRegistration &conversion : kConversions) {
        if (conversion.source == TypeCode::Array) {
            arrayConversions.insert(conversion.name);
        }
    }
    auto &registry = camel::core::OperatorTable::instance();
    for (const auto &[name, op] : getOpsImplMap()) {
        const std::string_view key(name);
        // Every operator under `op/` is pure except the assignment family, which writes a
        // variable. Every operator over Map and Set (coll/) returns a new value and changes
        // nothing.
        const bool pure = (key.starts_with("op/") && !storesIntoCell(key)) ||
                          key.starts_with("coll/") || key.starts_with("enum/") ||
                          kPureHelpers.contains(key);
        // Declared either way: an operator without a declaration is treated the same as one
        // declared to have effects, but only a declaration says that somebody looked.
        registry.set(
            ":" + name,
            {.pure = pure, .elementwise = arrayConversions.contains(key), .mayFault = !kNeverFaults.contains(key)});
    }
    // `extent_of` is the one operator declared without a kernel of its own: a call it accepts is
    // answered where it is built, so no path makes a node for a run to execute
    // (`src/builtin/operators/struct.h`). It is still an operator a program writes and a module
    // declares (`src/builtin/modules/globals.cpp#extentOfResolver`), so it states its purity like
    // every other one -- the loop above registers the operators that have an implementation, and
    // this one has none by design rather than by omission. It reads a type and nothing else, so it
    // is pure, it is not elementwise, and a run never reaches it to fault. The three answers are
    // one decision and not three defaults: if a kernel is ever written for this operator, `mayFault`
    // becomes a guard that says nothing about a path that now exists unless all three are decided
    // again together.
    registry.set(":op/extent_of", {.pure = true, .elementwise = false, .mayFault = false});
    // Which types each conversion reads and writes: the fact a reader asks for instead of
    // decoding the letters of the URI (`src/core/forward.cpp#conversionKeyOf`). Written here,
    // where the operator is registered, and at no second place.
    for (const ConversionRegistration &conversion : kConversions) {
        registry.declareConversion(
            std::string(":") + conversion.name, {conversion.source, conversion.target});
    }
    // The higher-order operators and function composition run a function the program passes
    // in: what they do is what that function does, and the passes that look at them look at
    // that function. exit and sleep are the process module's and are built in as well.
    for (const char *uri :
         {":mark/map_arr",
          ":mark/filter_arr",
          ":mark/reduce_arr",
          ":mark/foreach_arr",
          ":mark/unordered_reduce_arr",
          ":mark/unordered_foreach_arr"}) {
        registry.set(uri, {.pure = false, .callsGraphs = true});
    }
    // The fault-isolation node calls the function it is given, and its result depends on
    // whether that call faults: never folded, merged or dropped as a pure call would be.
    registry.set(
        std::string(camel::execute::kFaultIsolationUri),
        {.pure = false, .callsGraphs = true});
    // The transform that gives the function of that node (:mark/attempt) is read by the builder,
    // which turns `attempt(f)` into the value itself: no graph ever holds this node, and the
    // declaration is the conservative one, as for the operator the value calls.
    registry.set(
        std::string(camel::execute::kAttemptTransformUri),
        {.pure = false, .callsGraphs = true});
    // The block counters are ambient state: two calls with the same arguments can return
    // different structs, so the call must not be folded, merged or dropped as a pure one.
    registry.set(":op/mem_blocks", {.pure = false});
    // Creating and reading a variable changes nothing outside the program's variables.
    registry.set(":var/new", {.stateOnly = true});
    registry.set(":var/get", {.stateOnly = true});
    registry.set(":var/after", {.stateOnly = true});
    registry.set(":var/peek", {.stateOnly = true});
    registry.set(":var/take", {.stateOnly = true});
    // The in-place element set of a path assignment (Builder::pathAssignmentOf) writes the
    // array `:var/take` handed it and returns it; it changes nothing outside the program's own
    // variables, and an index out of bounds is a fault.
    registry.set(":op/set_index_in_place", {.stateOnly = true, .mayFault = true});
    registry.setInPlaceForm(":op/set_index", ":op/set_index_in_place");
    registry.declareReusesOperand(":op/set_index_in_place");
    // The in-place append (Builder::inPlaceRebindingOf): the array :var/take handed it is
    // appended to and returned; it changes nothing outside the program's own variables.
    registry.set(":struct/append_arr_in_place", {.stateOnly = true});
    registry.setInPlaceForm(":struct/append_arr", ":struct/append_arr_in_place");
    registry.declareReusesOperand(":struct/append_arr_in_place");
    // What the compiler does at a call because of the operator (design principles log 2.17).
    registry.declareCallSpec(":op/error_new", {.callSiteAtArity = 2});
    registry.declareCallSpec(":mark/foreach_arr", {.discardsCallbackResult = true});
    registry.declareCallSpec(":mark/unordered_foreach_arr", {.discardsCallbackResult = true});
    for (const char *uri : {":io/print", ":io/println"}) {
        registry.declareCallSpec(uri, {.formatWithArg = 0, .checkFormat = checkFormatPattern});
    }
    registry.declareCallSpec(
        ":str/format",
        {.formatNormArg = 0, .checkFormat = checkFormatPattern});
    declareBuiltinOperatorSemantics();
    std::vector<std::string_view> names;
    for (const auto &[name, op] : getOpsImplMap()) {
        names.push_back(name);
    }
    declareBuiltinOperatorResources(names);
}

BasicBuiltinExecutor::BasicBuiltinExecutor(context_ptr_t ctx) : Executor(ctx, getOpsImplMap()) {}

executor_ptr_t BasicBuiltinExecutor::create(context_ptr_t ctx) {
    return std::make_shared<BasicBuiltinExecutor>(ctx);
}
