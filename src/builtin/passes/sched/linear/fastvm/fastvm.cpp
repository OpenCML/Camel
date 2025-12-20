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
#include "utils/opperf.h"

using namespace std;
using namespace GraphIR;

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
