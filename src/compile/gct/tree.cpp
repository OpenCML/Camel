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
 * Created: Oct. 13, 2026
 * Updated: Oct. 13, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The node factory of the graph construction tree (camel/compile/gct/tree.h).
 */

#include "camel/compile/gct/tree.h"

#include <memory>
#include <ostream>
#include <utility>

namespace camel::compile::gct {

namespace {

template <typename LoadT, typename... Args> node_ptr_t make(Args &&...args) {
    return std::make_shared<Node>(
        std::static_pointer_cast<Load>(std::make_shared<LoadT>(std::forward<Args>(args)...)));
}

} // namespace

node_ptr_t Tree::makeData(const data_ptr_t &data) { return make<DataLoad>(data); }
node_ptr_t Tree::makeType(Type *type, ImplMark impl) { return make<TypeLoad>(type, impl); }
node_ptr_t Tree::makeDecl(const std::string &name, bool isFunc) {
    return make<DeclLoad>(name, isFunc);
}
node_ptr_t Tree::makeFunc(const std::string &name) { return make<FuncLoad>(name); }
node_ptr_t Tree::makeNRef(const std::string &name) { return make<NRefLoad>(name); }
node_ptr_t Tree::makeDRef(const std::string &name) { return make<DRefLoad>(Reference(name)); }
node_ptr_t Tree::makeVari() { return make<VariLoad>(); }
node_ptr_t Tree::makeWait() { return make<WaitLoad>(); }
node_ptr_t Tree::makeLink(size_t argCount) { return make<LinkLoad>(argCount); }
node_ptr_t Tree::makeWith(size_t argCount) { return make<WithLoad>(argCount); }
node_ptr_t Tree::makeAccs(size_t index) { return make<AccsLoad>(index); }
node_ptr_t Tree::makeAccs(const std::string &key) { return make<AccsLoad>(key); }
node_ptr_t Tree::makeBrch(BranchKind kind) { return make<BrchLoad>(kind); }
node_ptr_t Tree::makeCase(CaseType type, const std::string &binding) {
    return make<CaseLoad>(type, binding);
}
node_ptr_t Tree::makeCast(Type *targetType, bool annotation) {
    return make<CastLoad>(targetType, annotation);
}
node_ptr_t Tree::makeAnno() { return make<AnnoLoad>(std::string()); }
node_ptr_t Tree::makeExit(ExitType type) { return make<ExitLoad>(type); }
node_ptr_t Tree::makeExec(bool synced, bool operands) { return make<ExecLoad>(synced, operands); }
node_ptr_t Tree::makeExpt(const std::vector<Reference> &exports) { return make<ExptLoad>(exports); }

node_ptr_t Tree::clone(const node_ptr_t &node) { return node->clone(); }

void Tree::print(std::ostream &os) const {
    if (root_) {
        root_->print(os);
    }
}

} // namespace camel::compile::gct
