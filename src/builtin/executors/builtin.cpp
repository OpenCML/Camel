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
 * Author: Zhenjie Wei
 * Created: Sep. 09, 2025
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "builtin.h"
#include "camel/core/context/frame.h"
#include "camel/core/error/diagnostics.h"
#include "camel/execute/fault_isolation.h"
#include "camel/utils/log.h"

using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::rtdata;

#include "../operators/cast.h"
#include "../operators/collection.h"
#include "../operators/enum.h"
#include "../operators/higher_order.h"
#include "../operators/io.h"
#include "../operators/macro.h"
#include "../operators/mem.h"
#include "../operators/ops.h"
#include "../operators/other.h"
#include "../operators/profiler.h"
#include "../operators/set_value.h"
#include "../operators/str.h"
#include "../operators/struct.h"
#include "../operators/tagged.h"
#include "camel/core/operator_traits.h"

#include <string_view>
#include <unordered_set>

const std::unordered_map<std::string, operator_t> &getOpsImplMap() {
    static const std::unordered_map<std::string, operator_t> map = {
        // The placeholder for an operator that is declared before it is written. Nothing below
        // maps to it: a combination of operator and type without an implementation has no
        // resolver either (globals.cpp), so the compiler rejects it. Should one be mapped to it
        // again, running it is a located fault, never a silent null.
        {"not-impl", __not_implemented__},

        // Type conversions
        {"op/itoi", __itoi__},
        {"op/ltoi", __ltoi__},
        {"op/ftoi", __ftoi__},
        {"op/dtoi", __dtoi__},
        {"op/stoi", __stoi__},

        {"op/itol", __itol__},
        {"op/ltol", __ltol__},
        {"op/ftol", __ftol__},
        {"op/dtol", __dtol__},
        {"op/stol", __stol__},

        {"op/itof", __itof__},
        {"op/ltof", __ltof__},
        {"op/ftof", __ftof__},
        {"op/dtof", __dtof__},
        {"op/stof", __stof__},

        {"op/itod", __itod__},
        {"op/ltod", __ltod__},
        {"op/ftod", __ftod__},
        {"op/dtod", __dtod__},
        {"op/stod", __stod__},

        {"op/itoc", __itoc__},
        {"op/ltoc", __ltoc__},
        {"op/ftoc", __ftoc__},
        {"op/dtoc", __dtoc__},
        {"op/ctoc", __ctoc__},
        {"op/ztoc", __ztoc__},

        {"op/itoz", __itoz__},
        {"op/ltoz", __ltoz__},
        {"op/ftoz", __ftoz__},
        {"op/dtoz", __dtoz__},
        {"op/ctoz", __ctoz__},
        {"op/ztoz", __ztoz__},

        {"op/itos", __itos__},
        {"op/ltos", __ltos__},
        {"op/ftos", __ftos__},
        {"op/dtos", __dtos__},
        {"op/stos", __stos__},
        {"op/ctos", __atos__},
        {"op/ztos", __atos__},
        {"op/atos", __atos__},

        // diagnostics
        {"op/mem_blocks", __builtin__mem_blocks__},
        // `xs->append<v>` written by the loop-state rewrite; not a group a program can name.
        {"struct/append_arr_owned", __append_owned_arr__},
        // The private copy of an array a rewritten loop starts from; likewise internal.
        {"struct/copy_arr", __copy_arr__},

        // variables
        {"var/new", __builtin__var_new__},
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
        {"op/assn_append", __builtin__assn_append__},
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

        {"op/ne_i", __builtin__ne__},
        {"op/ne_l", __builtin__ne__},
        {"op/ne_f", __builtin__ne__},
        {"op/ne_d", __builtin__ne__},
        {"op/ne_b", __builtin__ne__},
        {"op/ne_s", __builtin__ne__},
        {"op/ne_c", __builtin__ne__},
        {"op/ne_z", __builtin__ne__},
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

        {"op/not", __builtin__not__},

        {"op/neg_i", __builtin__neg__},
        {"op/neg_l", __builtin__neg__},
        {"op/neg_f", __builtin__neg__},
        {"op/neg_d", __builtin__neg__},
        {"op/neg_c", __builtin__neg_c__},
        {"op/neg_z", __builtin__neg_z__},

        {"op/inv_i", __builtin__inv__},
        {"op/inv_l", __builtin__inv__},

        // IO (built-in input/print/println)
        {"io/input", __op_input__},
        {"io/print", __op_print__},
        {"io/println", __op_println__},

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
        {"mark/apply_arr", __map_arr__},
        {"mark/filter_arr", __filter_arr__},
        {"mark/reduce_arr", __reduce_arr__},
        {"mark/unordered_reduce_arr", __reduce_arr__},
        {"mark/foreach_arr", __foreach_arr__},
        {"mark/unordered_foreach_arr", __foreach_arr__},

        {"struct/zip", __zip__},

        {"struct/head_arr", __head_arr__},

        {"struct/tail_arr", __tail_arr__},

        {"struct/range", __range__},

        {"struct/slice_arr", __slice_arr__},

        {"struct/concat_arr", __concat_arr__},

        {"struct/append_arr", __append_arr__},

        {"struct/extend_arr", __extend_arr__},

        {"struct/contains_arr", __contains_arr__},
        {"struct/contains_str", __contains_str__},

        // Tangent arrays, inserted by autodiff into gradient graphs, never written by a program.
        {"tangent/add_arr", __tangent_add_arr__},
        {"tangent/zeros_arr", __tangent_zeros_arr__},
        {"tangent/scatter_add_arr", __tangent_scatter_add_arr__},
        {"tangent/init_arr", __tangent_init_arr__},
        {"tangent/last_arr", __tangent_last_arr__},

        // profiler
        {"profiler/begin", __profiler_begin__},
        {"profiler/end", __profiler_end__},
        {"profiler/instant", __profiler_instant__},
        {"profiler/enable", __profiler_enable__},

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
        {"coll/neq", __coll_neq__},
    };
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
        "struct/head_arr",
        "struct/tail_arr",
        "struct/range",
        "struct/slice_arr",
        "struct/concat_arr",
        "struct/append_arr",
        "struct/extend_arr",
        "struct/contains_arr",
        "struct/contains_str",
        "tangent/add_arr",
        "tangent/zeros_arr",
        "tangent/scatter_add_arr",
        "tangent/init_arr",
        "tangent/last_arr",
    };
    // Declared never to fault, where the implementation has no fault path at all: integer
    // addition and subtraction wrap, floating-point arithmetic and ordering cannot fail, and
    // the strict boolean operators only read their operands. Everything else keeps the
    // default (may fault). No pass acts on this declaration yet (operator_table.h).
    static const std::unordered_set<std::string_view> kNeverFaults = {
        "op/add_i", "op/add_l", "op/add_f", "op/add_d", "op/sub_i", "op/sub_l", "op/sub_f",
        "op/sub_d", "op/lt_i",  "op/lt_l",  "op/lt_f",  "op/lt_d",  "op/le_i",  "op/le_l",
        "op/le_f",  "op/le_d",  "op/gt_i",  "op/gt_l",  "op/gt_f",  "op/gt_d",  "op/ge_i",
        "op/ge_l",  "op/ge_f",  "op/ge_d",  "op/and",   "op/or",    "op/not",
    };
    auto &registry = camel::core::OperatorTable::instance();
    for (const auto &[name, op] : getOpsImplMap()) {
        const std::string_view key(name);
        // Every operator over Map and Set (coll/) returns a new value and changes nothing.
        const bool pure = (key.starts_with("op/") && !storesIntoCell(key)) ||
                          key.starts_with("coll/") || key.starts_with("enum/") ||
                          kPureHelpers.contains(key);
        // Declared either way: an operator without a declaration is treated the same as one
        // declared to have effects, but only a declaration says that somebody looked.
        registry.set(
            ":" + name,
            {.pure = pure, .elementwise = false, .mayFault = !kNeverFaults.contains(key)});
    }
    // The higher-order operators and function composition run a function the program passes
    // in: what they do is what that function does, and the passes that look at them look at
    // that function. exit and sleep are the os module's and are built in as well.
    for (const char *uri :
         {":mark/map_arr",
          ":mark/apply_arr",
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
    registry.declare("os", {{"exit", false}, {"sleep", false}});
    // The block counters are ambient state: two calls with the same arguments can return
    // different structs, so the call must not be folded, merged or dropped as a pure one.
    registry.set(":op/mem_blocks", {.pure = false});
    // Creating and reading a variable changes nothing outside the program's variables.
    registry.set(":var/new", {.stateOnly = true});
    registry.set(":var/get", {.stateOnly = true});
    registry.set(":var/peek", {.stateOnly = true});
    registry.set(":var/take", {.stateOnly = true});
    // The in-place element set of a path assignment (Builder::pathAssignmentOf) writes the
    // array `:var/take` handed it and returns it; it changes nothing outside the program's own
    // variables, and an index out of bounds is a fault.
    registry.set(":op/set_index_in_place", {.stateOnly = true, .mayFault = true});
    registry.setInPlaceForm(":op/set_index", ":op/set_index_in_place");
    registry.declareReusesOperand(":op/set_index_in_place");
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
