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
 * Created: Sep. 08, 2025
 * Updated: Dec. 20, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "fastvm.h"
#include "builtin/algo/topo.h"
#include "core/data/primary.h"
#include "core/rtdata/base.h"
#include "utils/opperf.h"
#include <cstddef>

#ifndef NDEBUG
#include "service/profiler/advanced/advanced_tracer.h"
#include "service/profiler/core/trace.h"
#endif

#define NEXT()                                                                                     \
    do {                                                                                           \
        pc += bc->opsize;                                                                          \
        bc = &base[pc];                                                                            \
        goto *dispatchTable[static_cast<size_t>(bc->opcode)];                                      \
    } while (0)

#define JUMP()                                                                                     \
    do {                                                                                           \
        bc = &base[pc];                                                                            \
        goto *dispatchTable[static_cast<size_t>(bc->opcode)];                                      \
    } while (0)

using namespace std;
using namespace GraphIR;

inline void evalInlinedOpCode(const Bytecode &bc, Frame *currFrame, const context_ptr_t &context) {
    slot_t lhs = currFrame->get<slot_t>(bc.fastop[0]);
    slot_t rhs = currFrame->get<slot_t>(bc.fastop[1]);

    switch (static_cast<DenseOpCode>(bc.opcode)) {
    // 加法
    case DenseOpCode::IADD: {
        int32_t res = fromSlot<Int>(lhs) + fromSlot<Int>(rhs);
        currFrame->set(bc.result, res);
        break;
    }
    case DenseOpCode::LADD: {
        int64_t res = fromSlot<Long>(lhs) + fromSlot<Long>(rhs);
        currFrame->set(bc.result, res);
        break;
    }
    case DenseOpCode::FADD: {
        float res = fromSlot<Float>(lhs) + fromSlot<Float>(rhs);
        currFrame->set(bc.result, res);
        break;
    }
    case DenseOpCode::DADD: {
        double res = fromSlot<Double>(lhs) + fromSlot<Double>(rhs);
        currFrame->set(bc.result, res);
        break;
    }

    // 减法
    case DenseOpCode::ISUB: {
        int32_t res = fromSlot<Int>(lhs) - fromSlot<Int>(rhs);
        currFrame->set(bc.result, res);
        break;
    }
    case DenseOpCode::LSUB: {
        int64_t res = fromSlot<Long>(lhs) - fromSlot<Long>(rhs);
        currFrame->set(bc.result, res);
        break;
    }
    case DenseOpCode::FSUB: {
        float res = fromSlot<Float>(lhs) - fromSlot<Float>(rhs);
        currFrame->set(bc.result, res);
        break;
    }
    case DenseOpCode::DSUB: {
        double res = fromSlot<Double>(lhs) - fromSlot<Double>(rhs);
        currFrame->set(bc.result, res);
        break;
    }

    // 乘法
    case DenseOpCode::IMUL: {
        int32_t res = fromSlot<Int>(lhs) * fromSlot<Int>(rhs);
        currFrame->set(bc.result, res);
        break;
    }
    case DenseOpCode::LMUL: {
        int64_t res = fromSlot<Long>(lhs) * fromSlot<Long>(rhs);
        currFrame->set(bc.result, res);
        break;
    }
    case DenseOpCode::FMUL: {
        float res = fromSlot<Float>(lhs) * fromSlot<Float>(rhs);
        currFrame->set(bc.result, res);
        break;
    }
    case DenseOpCode::DMUL: {
        double res = fromSlot<Double>(lhs) * fromSlot<Double>(rhs);
        currFrame->set(bc.result, res);
        break;
    }

    // 除法（带除零检测）
    case DenseOpCode::IDIV: {
        int32_t divisor = fromSlot<Int>(rhs);
        if (divisor == 0) {
            context->rtmDiags()->of(RuntimeDiag::DivisionByZero).commit();
        }
        int32_t res = fromSlot<Int>(lhs) / divisor;
        currFrame->set(bc.result, res);
        break;
    }
    case DenseOpCode::LDIV: {
        int64_t divisor = fromSlot<Long>(rhs);
        if (divisor == 0) {
            context->rtmDiags()->of(RuntimeDiag::DivisionByZero).commit();
        }
        int64_t res = fromSlot<Long>(lhs) / divisor;
        currFrame->set(bc.result, res);
        break;
    }
    case DenseOpCode::FDIV: {
        float divisor = fromSlot<Float>(rhs);
        if (divisor == 0.0f) {
            context->rtmDiags()->of(RuntimeDiag::DivisionByZero).commit();
        }
        float res = fromSlot<Float>(lhs) / divisor;
        currFrame->set(bc.result, res);
        break;
    }
    case DenseOpCode::DDIV: {
        double divisor = fromSlot<Double>(rhs);
        if (divisor == 0.0) {
            context->rtmDiags()->of(RuntimeDiag::DivisionByZero).commit();
        }
        double res = fromSlot<Double>(lhs) / divisor;
        currFrame->set(bc.result, res);
        break;
    }

    // 比较 (<, >, ==, !=, <=, >=)
    case DenseOpCode::ILT: {
        bool res = fromSlot<Int>(lhs) < fromSlot<Int>(rhs);
        currFrame->set(bc.result, res);
        break;
    }
    case DenseOpCode::LLT: {
        bool res = fromSlot<Long>(lhs) < fromSlot<Long>(rhs);
        currFrame->set(bc.result, res);
        break;
    }
    case DenseOpCode::FLT: {
        bool res = fromSlot<Float>(lhs) < fromSlot<Float>(rhs);
        currFrame->set(bc.result, res);
        break;
    }
    case DenseOpCode::DLT: {
        bool res = fromSlot<Double>(lhs) < fromSlot<Double>(rhs);
        currFrame->set(bc.result, res);
        break;
    }

    case DenseOpCode::IGT: {
        bool res = fromSlot<Int>(lhs) > fromSlot<Int>(rhs);
        currFrame->set(bc.result, res);
        break;
    }
    case DenseOpCode::LGT: {
        bool res = fromSlot<Long>(lhs) > fromSlot<Long>(rhs);
        currFrame->set(bc.result, res);
        break;
    }
    case DenseOpCode::FGT: {
        bool res = fromSlot<Float>(lhs) > fromSlot<Float>(rhs);
        currFrame->set(bc.result, res);
        break;
    }
    case DenseOpCode::DGT: {
        bool res = fromSlot<Double>(lhs) > fromSlot<Double>(rhs);
        currFrame->set(bc.result, res);
        break;
    }

    case DenseOpCode::IEQ: {
        bool res = fromSlot<Int>(lhs) == fromSlot<Int>(rhs);
        currFrame->set(bc.result, res);
        break;
    }
    case DenseOpCode::LEQ: {
        bool res = fromSlot<Long>(lhs) == fromSlot<Long>(rhs);
        currFrame->set(bc.result, res);
        break;
    }
    case DenseOpCode::FEQ: {
        bool res = fromSlot<Float>(lhs) == fromSlot<Float>(rhs);
        currFrame->set(bc.result, res);
        break;
    }
    case DenseOpCode::DEQ: {
        bool res = fromSlot<Double>(lhs) == fromSlot<Double>(rhs);
        currFrame->set(bc.result, res);
        break;
    }

    case DenseOpCode::INE: {
        bool res = fromSlot<Int>(lhs) != fromSlot<Int>(rhs);
        currFrame->set(bc.result, res);
        break;
    }
    case DenseOpCode::LNE: {
        bool res = fromSlot<Long>(lhs) != fromSlot<Long>(rhs);
        currFrame->set(bc.result, res);
        break;
    }
    case DenseOpCode::FNE: {
        bool res = fromSlot<Float>(lhs) != fromSlot<Float>(rhs);
        currFrame->set(bc.result, res);
        break;
    }
    case DenseOpCode::DNE: {
        bool res = fromSlot<Double>(lhs) != fromSlot<Double>(rhs);
        currFrame->set(bc.result, res);
        break;
    }

    case DenseOpCode::ILE: {
        bool res = fromSlot<Int>(lhs) <= fromSlot<Int>(rhs);
        currFrame->set(bc.result, res);
        break;
    }
    case DenseOpCode::LLE: {
        bool res = fromSlot<Long>(lhs) <= fromSlot<Long>(rhs);
        currFrame->set(bc.result, res);
        break;
    }
    case DenseOpCode::FLE: {
        bool res = fromSlot<Float>(lhs) <= fromSlot<Float>(rhs);
        currFrame->set(bc.result, res);
        break;
    }
    case DenseOpCode::DLE: {
        bool res = fromSlot<Double>(lhs) <= fromSlot<Double>(rhs);
        currFrame->set(bc.result, res);
        break;
    }

    case DenseOpCode::IGE: {
        bool res = fromSlot<Int>(lhs) >= fromSlot<Int>(rhs);
        currFrame->set(bc.result, res);
        break;
    }
    case DenseOpCode::LGE: {
        bool res = fromSlot<Long>(lhs) >= fromSlot<Long>(rhs);
        currFrame->set(bc.result, res);
        break;
    }
    case DenseOpCode::FGE: {
        bool res = fromSlot<Float>(lhs) >= fromSlot<Float>(rhs);
        currFrame->set(bc.result, res);
        break;
    }
    case DenseOpCode::DGE: {
        bool res = fromSlot<Double>(lhs) >= fromSlot<Double>(rhs);
        currFrame->set(bc.result, res);
        break;
    }
    default:
        context->rtmDiags()
            ->of(RuntimeDiag::UnsupportedBytecode)
            .commit(to_string(bc.opcode, true));
    }
}

graph_ptr_t FastVMSchedPass::apply(graph_ptr_t &graph, std::ostream &os) {
    if (!graph->hasOutput()) {
        context_->rtmDiags()
            ->of(RuntimeDiag::MissingMainFunction)
            .commit(context_->mainModule()->name());
    }

    precompile(graph.get());

    pcStack_.clear();
    frameStack_.clear();

    opperf::start();

    size_t pc    = offsetMap_.at(graph.get());
    Frame *frame = framePool_.acquire(graph.get());
    push(pc, frame);
    call(pc, frame);
    pop();
    framePool_.release(frame);

    opperf::stop();
    opperf::report(std::cout);

    return Graph::null();
}

// slot_t FastVMSchedPass::call(size_t pc, Frame *rootFrame) {
//     Frame *currFrame = rootFrame;

//     while (true) {
//         const Bytecode &bc = bytecodes_[pc];
//         EXEC_WHEN_DEBUG(
//             l.in("FastVM").debug("Executing bytecode: {}", opCodeToString(bc, pc, context_,
//             true)));

// #ifdef OPPERF_ENABLED
//         std::string tag;
//         if (bc.opcode == DenseOpCode::OPER) {
//             tag = context_->execMgr().getNameOfAnOperator(bc.extra()->func);
//         } else if (bc.opcode == DenseOpCode::FUNC) {
//             tag = bc.extra()->graph->name();
//         }
//         opperf::ScopeTimer _timer(bc.opcode, tag);
// #else
//         opperf::ScopeTimer _timer(bc.opcode);
// #endif

//         switch (static_cast<DenseOpCode>(bc.opcode)) {
//         case DenseOpCode::RETN: {
//             slot_t result = currFrame->get<slot_t>(bc.fastop[0]);

//             if (currFrame == rootFrame) {
//                 return result;
//             }

//             framePool_.release(currFrame);

//             auto [lastPC, lastFrame] = pop();
//             pc                       = lastPC;
//             currFrame                = lastFrame;

//             Bytecode &lbc = bytecodes_[pc];
//             currFrame->set(lbc.result, result);

//             // 从下一条指令继续执行
//             pc += lbc.opsize;
//             continue;
//         } break;

//         case DenseOpCode::CAST: {
//             ASSERT(false, "CAST opcode not implemented in FastVM.");
//         } break;

//         case DenseOpCode::COPY: {
//             TypeCode srcType = currFrame->typeAt(bc.fastop[0]);
//             if (isGCTraced(srcType)) {
//                 Object *srcData = currFrame->get<Object *>(bc.fastop[0]);
//                 currFrame->set(bc.result, srcData->clone(mm::autoSpace(), false));
//             } else {
//                 slot_t srcData = currFrame->get<slot_t>(bc.fastop[0]);
//                 currFrame->set(bc.result, srcData);
//             }
//         } break;

//         case DenseOpCode::ACCS: {
//             TypeCode srcType = currFrame->typeAt(bc.fastop[0]);
//             if (srcType == TypeCode::Tuple) {
//                 Tuple *t = currFrame->get<Tuple *>(bc.fastop[0]);
//                 ASSERT(
//                     static_cast<size_t>(bc.fastop[1]) < t->size(),
//                     "Tuple access index out of range in FastVM.");
//                 currFrame->set(bc.result, t->get<slot_t>(static_cast<size_t>(bc.fastop[1])));
//             } else if (srcType == TypeCode::Struct) {
//                 Struct *s = currFrame->get<Struct *>(bc.fastop[0]);
//                 ASSERT(
//                     static_cast<size_t>(bc.fastop[1]) < s->size(),
//                     "Struct access index out of range in FastVM.");
//                 currFrame->set(bc.result, s->get<slot_t>(static_cast<size_t>(bc.fastop[1])));
//             } else {
//                 ASSERT(false, "ACCS opcode unsupported source type in FastVM.");
//             }
//         } break;

//         case DenseOpCode::JUMP: {
//             pc = static_cast<arr_size_t>(bc.fastop[0]);
//             continue; // skip i increment
//         } break;

//         case DenseOpCode::BRCH: {
//             const data_arr_t nargs = bc.nargs();
//             const data_arr_t wargs = bc.wargs();

//             size_t jumpIdx = 0;
//             if (bc.withCnt() == 0) {
//                 // 普通的 if-else 分支，cond 是 bool 类型
//                 bool condData = currFrame->get<bool>(nargs[0]);
//                 if (condData) {
//                     jumpIdx = 0; // jump to true branch
//                 } else {
//                     jumpIdx = 1; // jump to false branch
//                 }
//             } else {
//                 // match-case，依次判断各分支
//                 size_t j          = 0;
//                 TypeCode condType = currFrame->typeAt(nargs[0]);

//                 if (isGCTraced(condType)) {
//                     auto condData = currFrame->get<Object *>(nargs[0]);
//                     for (; j < bc.withCnt(); ++j) {
//                         auto caseData = currFrame->get<Object *>(wargs[j]);
//                         if (condData->equals(caseData)) {
//                             jumpIdx = j; // jump to matched case
//                             break;
//                         }
//                     }
//                 } else {
//                     auto condData = currFrame->get<slot_t>(nargs[0]);
//                     for (; j < bc.withCnt(); ++j) {
//                         auto caseData = currFrame->get<slot_t>(wargs[j]);
//                         if (condData == caseData) {
//                             jumpIdx = j; // jump to matched case
//                             break;
//                         }
//                     }
//                 }

//                 if (j == bc.withCnt()) {
//                     // fallthrough to else case if no match
//                     jumpIdx = bc.withCnt();
//                 }
//             }

//             currFrame->set(bc.result, fromSlot<Int>(jumpIdx));
//             pc += bc.opsize + jumpIdx;

//             continue; // skip i increment
//         } break;

//         case DenseOpCode::JOIN: {
//             const data_arr_t nargs = bc.nargs();
//             const data_arr_t wargs = bc.wargs();
//             int32_t brIndex        = currFrame->get<int32_t>(nargs[0]);
//             ASSERT(
//                 brIndex >= 0 && static_cast<size_t>(brIndex) < bc.withCnt(),
//                 "JOIN opcode choosen index out of range in FastVM.");
//             slot_t result = currFrame->get<slot_t>(wargs[static_cast<size_t>(brIndex)]);
//             currFrame->set(bc.result, result);
//         } break;

//         case DenseOpCode::FILL: {
//             const data_arr_t nargs = bc.nargs();
//             const data_arr_t wargs = bc.wargs();

//             TypeCode targetType = currFrame->typeAt(nargs[0]);
//             ASSERT(isGCTraced(targetType), "FILL target type is not GC-traced in FastVM.");

//             Object *target = currFrame->get<Object *>(nargs[0])->clone(mm::autoSpace());
//             ASSERT(target != nullptr, "FILL target data is null.");

//             switch (targetType) {
//             case TypeCode::Tuple: {
//                 const auto &type = currFrame->typePtrAt<TupleType>(bc.result);
//                 auto t           = static_cast<Tuple *>(target);
//                 const auto &refs = t->layout().refs();
//                 ASSERT(
//                     refs.size() == bc.withCnt(),
//                     std::format(
//                         "Tuple layout refs size mismatch in FastVM. Expected: {}, Actual: {}",
//                         bc.withCnt(),
//                         refs.size()));
//                 for (size_t j = 0; j < bc.withCnt(); ++j) {
//                     t->set<slot_t>(refs[j], currFrame->get<slot_t>(wargs[j]));
//                 }
//                 t->updateLayout(&type->layout());
//             } break;

//             case TypeCode::Array: {
//                 const auto &type = currFrame->typePtrAt<ArrayType>(bc.result);
//                 auto a           = static_cast<Array *>(target);
//                 const auto &refs = a->layout().refs();
//                 ASSERT(
//                     refs.size() == bc.withCnt(),
//                     std::format(
//                         "Array layout refs size mismatch in FastVM. Expected: {}, Actual: {}",
//                         bc.withCnt(),
//                         refs.size()));
//                 for (size_t j = 0; j < bc.withCnt(); ++j) {
//                     a->set<slot_t>(refs[j], currFrame->get<slot_t>(wargs[j]));
//                 }
//                 a->updateLayout(&type->layout());
//             } break;

//             case TypeCode::Struct: {
//                 const auto &type = currFrame->typePtrAt<StructType>(bc.result);
//                 auto s           = static_cast<Struct *>(target);
//                 const auto &refs = s->layout().refs();
//                 ASSERT(
//                     refs.size() == bc.withCnt(),
//                     std::format(
//                         "Struct layout refs size mismatch in FastVM. Expected: {}, Actual: {}",
//                         bc.withCnt(),
//                         refs.size()));
//                 for (size_t j = 0; j < bc.withCnt(); ++j) {
//                     s->set<slot_t>(refs[j], currFrame->get<slot_t>(wargs[j]));
//                 }
//                 s->updateLayout(&type->layout());
//             } break;

//             case TypeCode::Function: {
//                 // const auto &type   = currFrame->typePtrAt<FunctionType>(bc.result);
//                 auto f             = static_cast<Function *>(target);
//                 Tuple *closureData = f->tuple();
//                 for (size_t j = 0; j < bc.withCnt(); ++j) {
//                     closureData->set<slot_t>(j, currFrame->get<slot_t>(wargs[j]));
//                 }
//             } break;

//             default:
//                 ASSERT(
//                     false,
//                     std::format(
//                         "Unsupported FILL target type {} in FastVM.",
//                         typeCodeToString(targetType)));
//             }

//             currFrame->set(bc.result, target);
//         } break;

//         case DenseOpCode::CALL: {
//             const data_arr_t nargs = bc.nargs();
//             const data_arr_t wargs = bc.wargs();
//             auto function          = currFrame->get<Function *>(wargs[0]);
//             auto targetGraph       = function->graph();

//             Frame *funcFrame = framePool_.acquire(targetGraph);

//             size_t i = 0;
//             for (; i < nargs.size; ++i) {
//                 funcFrame->set(i + 1, currFrame->get<slot_t>(nargs[i]));
//             }

//             Tuple *closureData = function->tuple();
//             for (size_t j = 0; j < closureData->size(); ++j) {
//                 funcFrame->set(i + j + 1, closureData->get<slot_t>(j));
//             }

//             _timer.pause();
//             const auto &result = call(offsetMap_.at(targetGraph), funcFrame);
//             _timer.resume();

//             framePool_.release(funcFrame);

//             currFrame->set(bc.result, result);
//         } break;

//         case DenseOpCode::FUNC: {
//             // 保存当前程序计数器和栈帧
//             push(pc, currFrame);

//             // 创建新的栈帧并设置参数
//             Frame *funcFrame       = framePool_.acquire(bc.extra()->graph);
//             size_t argsCnt         = bc.normCnt();
//             const data_idx_t *args = bc.operands();
//             for (size_t i = 0; i < argsCnt; ++i) {
//                 funcFrame->set(i + 1, currFrame->get<slot_t>(args[i]));
//             }

//             // 切换到目标图的字节码位置和栈帧
//             pc        = bc.fastop[1];
//             currFrame = funcFrame;

//             continue;
//         } break;

//         case DenseOpCode::TAIL: {
//             // 尾调用不保存程序计数器和栈帧
//             // 直接释放当前栈帧
//             // 如果当前栈帧和目标栈帧属于同一个图
//             // 栈帧池会自动复用
//             // 这间接实现了尾调用优化
//             FrameView lastFrame(currFrame);
//             framePool_.release(currFrame);

//             // 创建新的栈帧并设置参数
//             // 对于刚刚释放的栈帧，栈帧池会自动复用
//             // 所以这里 currFrame 就是目标栈帧
//             currFrame              = framePool_._acquire(bc.extra()->graph);
//             size_t argsCnt         = bc.normCnt();
//             const data_idx_t *args = bc.operands();
//             for (size_t i = 0; i < argsCnt; ++i) {
//                 // 注意，这里的 currFrame 已经被释放了
//                 // 但由于栈帧池不会对已经释放的栈帧进行格式化
//                 // 所以这里仍然可以安全地获取原栈帧的数据
//                 // 当然，需要通过 lastFrame 来获取数据
//                 // lastFrame 中保存了原栈帧的静态数据区指针
//                 currFrame->set(i + 1, lastFrame.get<slot_t>(args[i]));
//             }
//             // 这里需要手动 resetTop，因为 _acquire 不会 resetTop
//             // 之所以延迟 resetTop，是为了避免 resetTop 破坏刚刚释放的栈帧的数据
//             framePool_._resetTop();

//             // 切换到目标图的字节码位置
//             pc = bc.fastop[1];

//             continue;
//         } break;

//         case DenseOpCode::OPER: {
//             const data_arr_t nargs = bc.nargs();
//             const data_arr_t wargs = bc.wargs();
//             auto func              = bc.extra()->func;
//             EXEC_WHEN_DEBUG(l.in("FastVM").debug(
//                 "Executing operator {}.",
//                 context_->execMgr().getNameOfAnOperator(func)));
//             func(bc.result, nargs, wargs, *currFrame, *context_);
//         } break;

//         case DenseOpCode::SCHD: {
//             const data_arr_t nargs = bc.nargs();
//             const data_arr_t wargs = bc.wargs();
//             auto mark              = bc.extra()->mark;
//             evalMarkedOperator(mark, bc.result, nargs, wargs, *currFrame);
//         } break;

//         default: {
//             evalInlinedOpCode(bc, currFrame, context_);
//         }
//         }

//         // move to the next bytecode
//         pc += bc.opsize;
//     }
// }

slot_t FastVMSchedPass::call(size_t pc, Frame *rootFrame) {
    Frame *currFrame     = rootFrame;
    const Bytecode *base = bytecodes_.data();
    const Bytecode *bc;

    static void *dispatchTable[256] = {
        &&label_RETN, // 0
        &&label_CAST, // 1
        &&label_COPY, // 2
        &&label_ACCS, // 3
        &&label_JUMP, // 4
        &&label_BRCH, // 5
        &&label_JOIN, // 6
        &&label_FILL, // 7
        &&label_CALL, // 8
        &&label_FUNC, // 9
        &&label_TAIL, // 10
        &&label_OPER, // 11
        &&label_SCHD, // 12
        [13 ... 255] = &&label_DEFAULT,
    };

    // 初次分派
    JUMP();

label_RETN: {
    EXEC_WHEN_DEBUG(
        l.in("FastVM").debug("Executing bytecode: {}", opCodeToString(*bc, pc, context_, true)));
    opperf::ScopeTimer _timer(bc->opcode);

    slot_t result = currFrame->get<slot_t>(bc->fastop[0]);

    if (currFrame == rootFrame) {
        return result;
    }

    framePool_.release(currFrame);

    auto [lastPC, lastFrame] = pop();
    pc                       = lastPC;
    bc                       = &bytecodes_[pc];
    currFrame                = lastFrame;

    Bytecode &lbc = bytecodes_[pc];
    currFrame->set(lbc.result, result);

    NEXT();
}

label_CAST: {
    EXEC_WHEN_DEBUG(
        l.in("FastVM").debug("Executing bytecode: {}", opCodeToString(*bc, pc, context_, true)));
    opperf::ScopeTimer _timer(bc->opcode);

    ASSERT(false, "CAST opcode not implemented in FastVM.");

    NEXT();
}

label_COPY: {
    EXEC_WHEN_DEBUG(
        l.in("FastVM").debug("Executing bytecode: {}", opCodeToString(*bc, pc, context_, true)));
    opperf::ScopeTimer _timer(bc->opcode);

    TypeCode srcType = currFrame->typeAt(bc->fastop[0]);
    if (isGCTraced(srcType)) {
        Object *srcData = currFrame->get<Object *>(bc->fastop[0]);
        currFrame->set(bc->result, srcData->clone(mm::autoSpace(), false));
    } else {
        slot_t srcData = currFrame->get<slot_t>(bc->fastop[0]);
        currFrame->set(bc->result, srcData);
    }

    NEXT();
}

label_ACCS: {
    EXEC_WHEN_DEBUG(
        l.in("FastVM").debug("Executing bytecode: {}", opCodeToString(*bc, pc, context_, true)));
    opperf::ScopeTimer _timer(bc->opcode);

    TypeCode srcType = currFrame->typeAt(bc->fastop[0]);
    if (srcType == TypeCode::Tuple) {
        Tuple *t = currFrame->get<Tuple *>(bc->fastop[0]);
        ASSERT(
            static_cast<size_t>(bc->fastop[1]) < t->size(),
            "Tuple access index out of range in FastVM.");
        currFrame->set(bc->result, t->get<slot_t>(static_cast<size_t>(bc->fastop[1])));
    } else if (srcType == TypeCode::Struct) {
        Struct *s = currFrame->get<Struct *>(bc->fastop[0]);
        ASSERT(
            static_cast<size_t>(bc->fastop[1]) < s->size(),
            "Struct access index out of range in FastVM.");
        currFrame->set(bc->result, s->get<slot_t>(static_cast<size_t>(bc->fastop[1])));
    } else {
        ASSERT(false, "ACCS opcode unsupported source type in FastVM.");
    }

    NEXT();
}

label_JUMP: {
    EXEC_WHEN_DEBUG(
        l.in("FastVM").debug("Executing bytecode: {}", opCodeToString(*bc, pc, context_, true)));
    opperf::ScopeTimer _timer(bc->opcode);

    pc = static_cast<arr_size_t>(bc->fastop[0]);

    JUMP();
}

label_BRCH: {
    EXEC_WHEN_DEBUG(
        l.in("FastVM").debug("Executing bytecode: {}", opCodeToString(*bc, pc, context_, true)));
    opperf::ScopeTimer _timer(bc->opcode);

    const data_arr_t nargs = bc->nargs();
    const data_arr_t wargs = bc->wargs();

    size_t jumpIdx = 0;
    if (bc->withCnt() == 0) {
        // 普通的 if-else 分支，cond 是 bool 类型
        bool condData = currFrame->get<bool>(nargs[0]);
        if (condData) {
            jumpIdx = 0; // jump to true branch
        } else {
            jumpIdx = 1; // jump to false branch
        }
    } else {
        // match-case，依次判断各分支
        size_t j          = 0;
        TypeCode condType = currFrame->typeAt(nargs[0]);

        if (isGCTraced(condType)) {
            auto condData = currFrame->get<Object *>(nargs[0]);
            for (; j < bc->withCnt(); ++j) {
                auto caseData = currFrame->get<Object *>(wargs[j]);
                if (condData->equals(caseData)) {
                    jumpIdx = j; // jump to matched case
                    break;
                }
            }
        } else {
            auto condData = currFrame->get<slot_t>(nargs[0]);
            for (; j < bc->withCnt(); ++j) {
                auto caseData = currFrame->get<slot_t>(wargs[j]);
                if (condData == caseData) {
                    jumpIdx = j; // jump to matched case
                    break;
                }
            }
        }

        if (j == bc->withCnt()) {
            // fallthrough to else case if no match
            jumpIdx = bc->withCnt();
        }
    }

    currFrame->set(bc->result, fromSlot<Int>(jumpIdx));
    pc += bc->opsize + jumpIdx;

    JUMP();
}

label_JOIN: {
    EXEC_WHEN_DEBUG(
        l.in("FastVM").debug("Executing bytecode: {}", opCodeToString(*bc, pc, context_, true)));
    opperf::ScopeTimer _timer(bc->opcode);

    const data_arr_t nargs = bc->nargs();
    const data_arr_t wargs = bc->wargs();
    int32_t brIndex        = currFrame->get<int32_t>(nargs[0]);
    ASSERT(
        brIndex >= 0 && static_cast<size_t>(brIndex) < bc->withCnt(),
        "JOIN opcode choosen index out of range in FastVM.");
    slot_t result = currFrame->get<slot_t>(wargs[static_cast<size_t>(brIndex)]);
    currFrame->set(bc->result, result);

    NEXT();
}

label_FILL: {
    EXEC_WHEN_DEBUG(
        l.in("FastVM").debug("Executing bytecode: {}", opCodeToString(*bc, pc, context_, true)));
    opperf::ScopeTimer _timer(bc->opcode);

    const data_arr_t nargs = bc->nargs();
    const data_arr_t wargs = bc->wargs();

    TypeCode targetType = currFrame->typeAt(nargs[0]);
    ASSERT(isGCTraced(targetType), "FILL target type is not GC-traced in FastVM.");

    Object *target = currFrame->get<Object *>(nargs[0])->clone(mm::autoSpace());
    ASSERT(target != nullptr, "FILL target data is null.");

    switch (targetType) {
    case TypeCode::Tuple: {
        const auto &type = currFrame->typePtrAt<TupleType>(bc->result);
        auto t           = static_cast<Tuple *>(target);
        const auto &refs = t->layout().refs();
        ASSERT(
            refs.size() == bc->withCnt(),
            std::format(
                "Tuple layout refs size mismatch in FastVM. Expected: {}, Actual: {}",
                bc->withCnt(),
                refs.size()));
        for (size_t j = 0; j < bc->withCnt(); ++j) {
            t->set<slot_t>(refs[j], currFrame->get<slot_t>(wargs[j]));
        }
        t->updateLayout(&type->layout());
    } break;

    case TypeCode::Array: {
        const auto &type = currFrame->typePtrAt<ArrayType>(bc->result);
        auto a           = static_cast<Array *>(target);
        const auto &refs = a->layout().refs();
        ASSERT(
            refs.size() == bc->withCnt(),
            std::format(
                "Array layout refs size mismatch in FastVM. Expected: {}, Actual: {}",
                bc->withCnt(),
                refs.size()));
        for (size_t j = 0; j < bc->withCnt(); ++j) {
            a->set<slot_t>(refs[j], currFrame->get<slot_t>(wargs[j]));
        }
        a->updateLayout(&type->layout());
    } break;

    case TypeCode::Struct: {
        const auto &type = currFrame->typePtrAt<StructType>(bc->result);
        auto s           = static_cast<Struct *>(target);
        const auto &refs = s->layout().refs();
        ASSERT(
            refs.size() == bc->withCnt(),
            std::format(
                "Struct layout refs size mismatch in FastVM. Expected: {}, Actual: {}",
                bc->withCnt(),
                refs.size()));
        for (size_t j = 0; j < bc->withCnt(); ++j) {
            s->set<slot_t>(refs[j], currFrame->get<slot_t>(wargs[j]));
        }
        s->updateLayout(&type->layout());
    } break;

    case TypeCode::Function: {
        auto f             = static_cast<Function *>(target);
        Tuple *closureData = f->tuple();
        for (size_t j = 0; j < bc->withCnt(); ++j) {
            closureData->set<slot_t>(j, currFrame->get<slot_t>(wargs[j]));
        }
    } break;

    default:
        ASSERT(
            false,
            std::format(
                "Unsupported FILL target type {} in FastVM.",
                typeCodeToString(targetType)));
    }

    currFrame->set(bc->result, target);

    NEXT();
}

label_CALL: {
    EXEC_WHEN_DEBUG(
        l.in("FastVM").debug("Executing bytecode: {}", opCodeToString(*bc, pc, context_, true)));
    opperf::ScopeTimer _timer(bc->opcode);

    const data_arr_t nargs = bc->nargs();
    const data_arr_t wargs = bc->wargs();
    auto function          = currFrame->get<Function *>(wargs[0]);
    auto targetGraph       = function->graph();

    Frame *funcFrame = framePool_.acquire(targetGraph);

    size_t i = 0;
    for (; i < nargs.size; ++i) {
        funcFrame->set(i + 1, currFrame->get<slot_t>(nargs[i]));
    }

    Tuple *closureData = function->tuple();
    for (size_t j = 0; j < closureData->size(); ++j) {
        funcFrame->set(i + j + 1, closureData->get<slot_t>(j));
    }

    _timer.pause();
    const auto &result = call(offsetMap_.at(targetGraph), funcFrame);
    _timer.resume();

    framePool_.release(funcFrame);

    currFrame->set(bc->result, result);

    NEXT();
}

label_FUNC: {
    EXEC_WHEN_DEBUG(
        l.in("FastVM").debug("Executing bytecode: {}", opCodeToString(*bc, pc, context_, true)));
    opperf::ScopeTimer _timer(bc->opcode);

    // 保存当前程序计数器和栈帧
    push(pc, currFrame);

    // 创建新的栈帧并设置参数
    Frame *funcFrame       = framePool_.acquire(bc->extra()->graph);
    size_t argsCnt         = bc->normCnt();
    const data_idx_t *args = bc->operands();
    for (size_t i = 0; i < argsCnt; ++i) {
        funcFrame->set(i + 1, currFrame->get<slot_t>(args[i]));
    }

    // 切换到目标图的字节码位置和栈帧
    pc        = bc->fastop[1];
    currFrame = funcFrame;

    JUMP();
}

label_TAIL: {
    EXEC_WHEN_DEBUG(
        l.in("FastVM").debug("Executing bytecode: {}", opCodeToString(*bc, pc, context_, true)));
    opperf::ScopeTimer _timer(bc->opcode);

    // 尾调用不保存程序计数器和栈帧
    // 直接释放当前栈帧
    // 如果当前栈帧和目标栈帧属于同一个图
    // 栈帧池会自动复用
    // 这间接实现了尾调用优化
    FrameView lastFrame(currFrame);
    framePool_.release(currFrame);

    // 创建新的栈帧并设置参数
    // 对于刚刚释放的栈帧，栈帧池会自动复用
    // 所以这里 currFrame 就是目标栈帧
    currFrame              = framePool_._acquire(bc->extra()->graph);
    size_t argsCnt         = bc->normCnt();
    const data_idx_t *args = bc->operands();
    for (size_t i = 0; i < argsCnt; ++i) {
        // 注意，这里的 currFrame 已经被释放了
        // 但由于栈帧池不会对已经释放的栈帧进行格式化
        // 所以这里仍然可以安全地获取原栈帧的数据
        // 当然，需要通过 lastFrame 来获取数据
        // lastFrame 中保存了原栈帧的静态数据区指针
        currFrame->set(i + 1, lastFrame.get<slot_t>(args[i]));
    }
    // 这里需要手动 resetTop，因为 _acquire 不会 resetTop
    // 之所以延迟 resetTop，是为了避免 resetTop 破坏刚刚释放的栈帧的数据
    framePool_._resetTop();

    // 切换到目标图的字节码位置
    pc = bc->fastop[1];

    JUMP();
}

label_OPER: {
    EXEC_WHEN_DEBUG(
        l.in("FastVM").debug("Executing bytecode: {}", opCodeToString(*bc, pc, context_, true)));
    opperf::ScopeTimer _timer(bc->opcode);

    const data_arr_t nargs = bc->nargs();
    const data_arr_t wargs = bc->wargs();
    auto func              = bc->extra()->func;
    EXEC_WHEN_DEBUG(l.in("FastVM").debug(
        "Executing operator {}.",
        context_->execMgr().getNameOfAnOperator(func)));
    func(bc->result, nargs, wargs, *currFrame, *context_);

    NEXT();
}

label_SCHD: {
    EXEC_WHEN_DEBUG(
        l.in("FastVM").debug("Executing bytecode: {}", opCodeToString(*bc, pc, context_, true)));
    opperf::ScopeTimer _timer(bc->opcode);

    const data_arr_t nargs = bc->nargs();
    const data_arr_t wargs = bc->wargs();
    auto mark              = bc->extra()->mark;
    evalMarkedOperator(mark, bc->result, nargs, wargs, *currFrame);

    NEXT();
}

label_DEFAULT:
    evalInlinedOpCode(*bc, currFrame, context_);

    NEXT();
}

void FastVMSchedPass::evalMarkedOperator(
    const MarkOpCode op, data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &currFrame) {
    switch (op) {
    case MarkOpCode::MapArr:
        evalMarkedOperator_map_arr(self, nargs, wargs, currFrame);
        break;
    case MarkOpCode::ApplyArr:
        evalMarkedOperator_apply_arr(self, nargs, wargs, currFrame);
        break;
    case MarkOpCode::FilterArr:
        evalMarkedOperator_filter_arr(self, nargs, wargs, currFrame);
        break;
    case MarkOpCode::ReduceArr:
        evalMarkedOperator_reduce_arr(self, nargs, wargs, currFrame);
        break;
    case MarkOpCode::ForeachArr:
        evalMarkedOperator_foreach_arr(self, nargs, wargs, currFrame);
        break;
    default:
        ASSERT(false, "Unsupported marked operator in FastVM.");
    }
}

void FastVMSchedPass::evalMarkedOperator_map_arr(
    data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &currFrame) {
    Array *arr     = currFrame.get<Array *>(nargs[0]);
    Function *func = currFrame.get<Function *>(wargs[0]);
    Tuple *closure = func->tuple();

    const auto &retArrType = currFrame.typePtrAt<ArrayType>(self);
    Array *res             = Array::create(retArrType->layout(), mm::autoSpace(), arr->size());

    slot_t *from = arr->data();
    slot_t *to   = res->data();
    for (size_t i = 0; i < arr->size(); ++i) {
        Frame *frame = framePool_.acquire(func->graph());

        frame->set(1, from[i]); // 设置第一个参数
        // 如果有闭包
        if (closure->size() > 0) {
            for (size_t j = 0; j < closure->size(); ++j) {
                frame->set(j + 2, closure->get<slot_t>(j));
            }
        }

        to[i] = call(offsetMap_.at(func->graph()), frame);
        framePool_.release(frame);
    }

    currFrame.set(self, res);
}

void FastVMSchedPass::evalMarkedOperator_apply_arr(
    data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &currFrame) {
    Array *arr     = currFrame.get<Array *>(nargs[0]);
    Function *func = currFrame.get<Function *>(wargs[0]);
    Tuple *closure = func->tuple();

    slot_t *data = arr->data();

    for (size_t i = 0; i < arr->size(); ++i) {
        Frame *frame = framePool_.acquire(func->graph());

        frame->set(1, data[i]);
        if (closure->size() > 0) {
            for (size_t j = 0; j < closure->size(); ++j) {
                frame->set(j + 2, closure->get<slot_t>(j));
            }
        }

        data[i] = call(offsetMap_.at(func->graph()), frame);
        framePool_.release(frame);
    }

    currFrame.set(self, arr);
}

void FastVMSchedPass::evalMarkedOperator_filter_arr(
    data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &currFrame) {
    Array *arr     = currFrame.get<Array *>(nargs[0]);
    Function *func = currFrame.get<Function *>(wargs[0]);
    Tuple *closure = func->tuple();

    const auto &retArrType = currFrame.typePtrAt<ArrayType>(self);
    Array *filtered        = Array::create(retArrType->layout(), mm::autoSpace(), arr->size());

    slot_t *from = arr->data();
    for (size_t i = 0; i < arr->size(); ++i) {
        Frame *frame = framePool_.acquire(func->graph());

        frame->set(1, from[i]);
        if (closure->size() > 0) {
            for (size_t j = 0; j < closure->size(); ++j) {
                frame->set(j + 2, closure->get<slot_t>(j));
            }
        }

        slot_t result = call(offsetMap_.at(func->graph()), frame);
        framePool_.release(frame);

        if (fromSlot<bool>(result)) {
            filtered->append(from[i]);
        }
    }

    filtered->shrinkToFit();

    currFrame.set(self, filtered);
}

void FastVMSchedPass::evalMarkedOperator_reduce_arr(
    data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &currFrame) {
    Array *arr     = currFrame.get<Array *>(nargs[0]);
    Function *func = currFrame.get<Function *>(wargs[0]);
    slot_t init    = currFrame.get<slot_t>(wargs[1]);
    Tuple *closure = func->tuple();

    // 空数组直接返回初始值
    if (arr->size() == 0) {
        currFrame.set(self, init);
        return;
    }

    slot_t acc   = init;
    slot_t *from = arr->data();

    for (size_t i = 0; i < arr->size(); ++i) {
        Frame *frame = framePool_.acquire(func->graph());

        // reduce(acc, cur)
        frame->set(1, acc);
        frame->set(2, from[i]);

        // 如果有闭包参数
        if (closure->size() > 0) {
            for (size_t j = 0; j < closure->size(); ++j) {
                frame->set(j + 3, closure->get<slot_t>(j));
            }
        }

        acc = call(offsetMap_.at(func->graph()), frame);
        framePool_.release(frame);
    }

    currFrame.set(self, acc);
}

void FastVMSchedPass::evalMarkedOperator_foreach_arr(
    data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &currFrame) {
    Array *arr     = currFrame.get<Array *>(nargs[0]);
    Function *func = currFrame.get<Function *>(wargs[0]);
    Tuple *closure = func->tuple();

    slot_t *from = arr->data();

    for (size_t i = 0; i < arr->size(); ++i) {
        Frame *frame = framePool_.acquire(func->graph());

        frame->set(1, from[i]);

        if (closure->size() > 0) {
            for (size_t j = 0; j < closure->size(); ++j) {
                frame->set(j + 2, closure->get<slot_t>(j));
            }
        }

        call(offsetMap_.at(func->graph()), frame);
        framePool_.release(frame);
    }

    // foreach 无返回值
    currFrame.set(self, NullSlot);
}
