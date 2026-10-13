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
 * Created: May. 05, 2024
 * Updated: Oct. 13, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/common/tree.h"
#include "camel/compile/gct/load.h"

namespace camel::compile::gct {

// =============================================================================
// Node: GCT tree node (CRTP over AbstractTreeNode)
// =============================================================================

class Node : public AbstractTreeNode<load_ptr_t, Node> {
  public:
    Node(load_ptr_t load) : AbstractTreeNode(load) {}
    virtual ~Node() = default;

    LoadType type() const { return load_->type(); }
    std::string toString() const { return load_->toString(); }

    template <typename T> node_ptr_t atAs(size_t index) const {
        ASSERT(index < children_.size(), "Index out of bounds");
        ASSERT(children_.at(index) != nullptr, "Child node is null");
        ASSERT(
            std::dynamic_pointer_cast<T>(children_.at(index)->load()),
            "Dynamic pointer cast failed");
        return children_.at(index);
    }

    template <typename LoadT> std::shared_ptr<LoadT> loadAs() {
        ASSERT(std::dynamic_pointer_cast<LoadT>(load_), "Load type cast failed");
        return std::dynamic_pointer_cast<LoadT>(load_);
    }
    template <typename LoadT> const std::shared_ptr<LoadT> loadAs() const {
        ASSERT(std::dynamic_pointer_cast<LoadT>(load_), "Load type does not match requested type");
        return std::dynamic_pointer_cast<LoadT>(load_);
    }

    // -------------------------------------------------------------------------
    // What a node carries, read from the node itself. Each accessor is for the
    // kinds named with it and asserts that; users of the tree read nodes through
    // these and do not look at the Load objects behind them.
    // -------------------------------------------------------------------------

    /// The origin of the node in the source mapping (camel/core/source/manager.h).
    origin_id_t origin() const { return load_->origin(); }
    void setOrigin(origin_id_t origin) { load_->setOrigin(origin); }

    /// DATA: the constant.
    const data_ptr_t &data() const { return as<DataLoad>(LoadType::DATA).data(); }

    /// TYPE: the type, and how a function of that type is implemented.
    Type *dataType() const { return as<TypeLoad>(LoadType::TYPE).dataType(); }
    ImplMark implMark() const { return as<TypeLoad>(LoadType::TYPE).implMark(); }

    /// DECL, FUNC, NREF, DREF: the name as written, qualified names with `::` between the
    /// parts. Empty for an anonymous FUNC.
    const std::string &name() const {
        switch (type()) {
        case LoadType::DECL:
            return as<DeclLoad>(LoadType::DECL).name();
        case LoadType::FUNC:
            return as<FuncLoad>(LoadType::FUNC).name();
        case LoadType::NREF:
            return as<NRefLoad>(LoadType::NREF).name();
        default:
            return as<DRefLoad>(LoadType::DREF).name();
        }
    }

    /// DECL: whether a function is declared (else a type).
    bool isFunc() const { return as<DeclLoad>(LoadType::DECL).isFunc(); }

    /// LINK, WITH: the number of arguments written.
    size_t argCount() const {
        return type() == LoadType::LINK ? as<LinkLoad>(LoadType::LINK).args()
                                        : as<WithLoad>(LoadType::WITH).args();
    }

    /// EXIT: how the function is left.
    ExitType exitType() const { return as<ExitLoad>(LoadType::EXIT).exitType(); }

    /// EXEC: a sync block; and a scope the lowering introduced (see ExecLoad).
    bool synced() const { return as<ExecLoad>(LoadType::EXEC).synced(); }
    bool operands() const { return as<ExecLoad>(LoadType::EXEC).operands(); }

    /// EXPT: the exported names.
    size_t exportCount() const { return as<ExptLoad>(LoadType::EXPT).names().size(); }
    const std::string &exportName(size_t index) const {
        return as<ExptLoad>(LoadType::EXPT).names().at(index);
    }

    /// ACCS: an element by position (`t.0`) or a field by name (`s.x`).
    bool accessByIndex() const { return as<AccsLoad>(LoadType::ACCS).isNum(); }
    size_t accessIndex() const { return as<AccsLoad>(LoadType::ACCS).number(); }
    const std::string &accessKey() const { return as<AccsLoad>(LoadType::ACCS).key(); }

    /// BRCH: what selects the arm.
    BranchKind branchKind() const { return as<BrchLoad>(LoadType::BRCH).kind(); }

    /// CASE: the pattern, and the name it binds (Bind and Error; empty otherwise).
    CaseType caseType() const { return as<CaseLoad>(LoadType::CASE).caseType(); }
    const std::string &binding() const { return as<CaseLoad>(LoadType::CASE).binding(); }

    /// CAST: the target type, and whether the conversion is the type written on a
    /// declaration (see CastLoad).
    Type *targetType() const { return as<CastLoad>(LoadType::CAST).targetType(); }
    bool annotation() const { return as<CastLoad>(LoadType::CAST).annotation(); }

  private:
    template <typename LoadT> const LoadT &as([[maybe_unused]] LoadType expected) const {
        ASSERT(load_->type() == expected, "The node is not of the kind this accessor is for");
        return static_cast<const LoadT &>(*load_);
    }
};

} // namespace camel::compile::gct
