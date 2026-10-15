// Hand-edited: what std::cpp writes for examples/accepted/a01_fib.cml, with the four scalar
// operator calls (`:op/le_l`, `:op/sub_l`, `:op/add_l`) written as C++ operators, which is what
// the unused table in src/passes/trans/cpp/lower.cpp describes. Everything else is as emitted.
// NOT emitter output; a yardstick for section 1.8.
//
// Build: bash docs/design/cpp-memory/tools/cppbuild.sh docs/design/cpp-memory/examples/handwritten/fib_direct.cpp
#include "camel/core/rtdata/base.h"
#include "camel/core/type.h"
#include "camel/passes/trans/cpp/cpp_bridge.h"
#include <iostream>

using namespace camel::core::type;
using namespace camel::core::rtdata;
using namespace camel::execute::cppbridge;

namespace generated {

inline BoundOperator &o_io_println() {
    static auto &op = bridge().bind(":io/println");
    return op;
}

static inline Int64 f_g_4(Int64 v_arg_0);
static inline Int64 f_g_6(Int64 v_arg_0_);
static inline Int64 f_fib(Int64 v_arg_0__);
static inline Int64 f_main();

static inline Int64 f_g_4(Int64 v_arg_0) { return v_arg_0; }

static inline Int64 f_g_6(Int64 v_arg_0_) {
    const Int64 v1_op_sub_l = v_arg_0_ - 1LL;
    const Int64 v2_fib      = f_fib(v1_op_sub_l);
    const Int64 v3_op_sub_l = v_arg_0_ - 2LL;
    const Int64 v4_fib      = f_fib(v3_op_sub_l);
    const Int64 v5_op_add_l = v2_fib + v4_fib;
    return v5_op_add_l;
}

static inline Int64 f_fib(Int64 v_arg_0__) {
    const Bool v1_op_le_l = v_arg_0__ <= 1LL;
    return (v1_op_le_l) ? (f_g_4(v_arg_0__)) : (f_g_6(v_arg_0__));
}

static inline Int64 f_main() {
    const Int64 v1_fib        = f_fib(32LL);
    const Int64 v2_io_println = o_io_println().call<Int64>({val(Type::Int64(), v1_fib)});
    const Int64 v3_value      = 0LL;
    return v3_value;
}

} // namespace generated

int main() {
    initialize();
    int ret = 0;
    try {
        ret = static_cast<int>(generated::f_main());
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        ret = 1;
    } catch (...) {
        std::cerr << "Unknown error" << std::endl;
        ret = 1;
    }
    finalize();
    return ret;
}
