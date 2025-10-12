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
 * Updated: Oct. 12, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <variant>

#include "common/ref.h"
#include "common/tree.h"
#include "core/data/data.h"
#include "core/func.h"
#include "utils/assert.h"

namespace GraphConstructTree {

class Node;
using node_ptr_t = std::shared_ptr<Node>;

class Load;
using load_ptr_t = std::shared_ptr<Load>;

enum class LoadType {
    DECL, //
    FUNC,
    DATA,
    TYPE,
    NREF,
    DREF,
    VARI,
    WAIT,
    LINK,
    WITH,
    ACCS,
    BRCH,
    CASE,
    CAST,
    ANNO,
    EXIT,
    EXEC,
    EXPT,
};

std::string to_string(LoadType type);

class Load {
  protected:
    LoadType type_;
    size_t tokenStart_;
    size_t tokenEnd_;

  public:
    Load(LoadType type) : type_(type) {}
    virtual ~Load() = default;

    void setToken(size_t start, size_t end) {
        tokenStart_ = start;
        tokenEnd_ = end;
    }

    LoadType type() const { return type_; }
    std::pair<size_t, size_t> range() const { return {tokenStart_, tokenEnd_}; }

    virtual const std::string toString() const { return to_string(type_); }
    virtual void visit() { throw std::runtime_error("Load::visit() not implemented"); };
};

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

    template <typename LoadType> std::shared_ptr<LoadType> loadAs() {
        ASSERT(std::dynamic_pointer_cast<LoadType>(load_), "Load type cast failed");
        return std::dynamic_pointer_cast<LoadType>(load_);
    }
    template <typename LoadType> const std::shared_ptr<LoadType> loadAs() const {
        ASSERT(
            std::dynamic_pointer_cast<LoadType>(load_),
            "Load type does not match requested type");
        return std::dynamic_pointer_cast<LoadType>(load_);
    }
};

class DataLoad : public Load {
    data_ptr_t data_;

  public:
    DataLoad(data_ptr_t data) : Load(LoadType::DATA), data_(data) {
        ASSERT(data != nullptr, "DataLoad cannot be constructed with a null data pointer");
    }

    data_ptr_t data() { return data_; }

    const std::string toString() const override;
};

class VariLoad : public Load {
  public:
    VariLoad() : Load(LoadType::VARI) {}

    // const std::string toString() const override;
};

class TypeLoad : public Load {
    type_ptr_t dataType_;

  public:
    TypeLoad(type_ptr_t type, ImplMark impl, const std::string &uri = "")
        : Load(LoadType::TYPE), dataType_(type), implMark_(impl), uri_(uri) {}
    type_ptr_t dataType() const { return dataType_; }

    ImplMark implMark() const { return implMark_; }
    const std::string &uri() const { return uri_; }

    const std::string toString() const override;

  private:
    ImplMark implMark_ = ImplMark::Graph;
    std::string uri_;
};

class DeclLoad : public Load {
    bool isFunc_;
    Reference ref_;

  public:
    DeclLoad(const Reference &ref, bool isFunc = false)
        : Load(LoadType::DECL), isFunc_(isFunc), ref_(ref) {}
    DeclLoad(const std::string &str, bool isFunc = false)
        : Load(LoadType::DECL), isFunc_(isFunc), ref_(str) {}

    bool isFunc() const { return isFunc_; }
    const Reference ref() const { return ref_; }

    const std::string toString() const override;
};

class FuncLoad : public Load {
    std::string name_;

  public:
    FuncLoad(const std::string name) : Load(LoadType::FUNC), name_(name) {}

    std::string name() const { return name_; }

    const std::string toString() const override;
};

class NRefLoad : public Load {
    Reference ref_;

  public:
    NRefLoad(const Reference &ref) : Load(LoadType::NREF), ref_(ref) {}
    NRefLoad(const std::string &str) : Load(LoadType::NREF), ref_(str) {}

    const Reference ref() const { return ref_; }

    const std::string toString() const override;
};

class DRefLoad : public Load {
    Reference ref_;

  public:
    DRefLoad(const Reference &ref) : Load(LoadType::DREF), ref_(ref) {}

    const Reference ref() const { return ref_; }

    const std::string toString() const override;
};

class WaitLoad : public Load {
  public:
    WaitLoad() : Load(LoadType::WAIT) {}

    // const std::string toString() const override;
};

class AnnoLoad : public Load {
    std::string annotation_;

  public:
    AnnoLoad(const std::string &annotation) : Load(LoadType::ANNO), annotation_(annotation) {}

    // const std::string toString() const override;
};

class LinkLoad : public Load {
  public:
    LinkLoad(size_t args = 0) : Load(LoadType::LINK), args_(args) {}

    void setArgs(size_t args) { args_ = args; }
    void addKwarg(const std::string &kwarg) { kwargs_.push_back(kwarg); }

    const std::string toString() const override {
        std::string result = "LINK: argcnt=" + std::to_string(args_);
        if (!kwargs_.empty()) {
            result += ", kwargs=[";
            for (const auto &kwarg : kwargs_) {
                result += kwarg + ", ";
            }
            result.pop_back(); // Remove last comma
            result.pop_back(); // Remove last space
            result += "]";
        }
        return result;
    }

  private:
    size_t args_;
    std::vector<std::string> kwargs_;
};

class WithLoad : public Load {
  public:
    WithLoad(size_t args = 0) : Load(LoadType::WITH), args_(args) {}

    void setArgs(size_t args) { args_ = args; }
    void addKwarg(const std::string &kwarg) { kwargs_.push_back(kwarg); }

    const std::string toString() const override {
        std::string result = "WITH: argcnt=" + std::to_string(args_);
        if (!kwargs_.empty()) {
            result += ", kwargs=[";
            for (const auto &kwarg : kwargs_) {
                result += kwarg + ", ";
            }
            result.pop_back(); // Remove last comma
            result.pop_back(); // Remove last space
            result += "]";
        }
        return result;
    }

  private:
    size_t args_;
    std::vector<std::string> kwargs_;
};

class ExitLoad : public Load {
  public:
    ExitLoad(ExitType type = ExitType::Return) : Load(LoadType::EXIT), exitType_(type) {}

    ExitType exitType() const { return exitType_; }

    const std::string toString() const override { return "EXIT: " + to_string(exitType_); }

  private:
    ExitType exitType_ = ExitType::Return;
};

class ExecLoad : public Load {
  public:
    ExecLoad(bool sync = false) : Load(LoadType::EXEC), synced_(sync) {}

    bool synced() const { return synced_; }

    const std::string toString() const override { return synced_ ? "SYNC" : "EXEC"; }

  private:
    bool synced_ = false;
};

class ExptLoad : public Load {
  public:
    ExptLoad(const std::vector<Reference> &exports) : Load(LoadType::EXPT), exports_(exports) {}

    const std::string toString() const override {
        std::string result = "EXPT: [";
        for (const auto &exp : exports_) {
            result += exp.toString() + ", ";
        }
        if (!exports_.empty()) {
            result.pop_back(); // Remove last comma
            result.pop_back(); // Remove last space
        }
        result += "]";
        return result;
    }

    const std::vector<Reference> &exports() const { return exports_; }

  private:
    std::vector<Reference> exports_;
};

class AccsLoad : public Load {
  public:
    AccsLoad(const std::string &index) : Load(LoadType::ACCS), index_(index) {}
    AccsLoad(size_t index) : Load(LoadType::ACCS), index_(index) {}

    bool isNum() const { return std::holds_alternative<size_t>(index_); }
    template <typename T> T index() const { return std::get<T>(index_); }
    std::variant<std::string, size_t> index() const { return index_; }

    const std::string toString() const override {
        std::string result = "ACCS: ";
        if (std::holds_alternative<size_t>(index_)) {
            result += std::to_string(std::get<size_t>(index_));
        } else {
            result += std::get<std::string>(index_);
        }
        return result;
    }

  private:
    std::variant<std::string, size_t> index_;
};

class BrchLoad : public Load {
  public:
    BrchLoad() : Load(LoadType::BRCH) {}

    // const std::string toString() const override;
};

class CaseLoad : public Load {
  public:
    enum class CaseType {
        Value,
        True,
        Else,
    };
    CaseLoad(CaseType type) : Load(LoadType::CASE), caseType_(type) {}

    CaseType caseType() const { return caseType_; }

    const std::string toString() const override {
        std::string result = "CASE: ";
        switch (caseType_) {
        case CaseType::Value:
            result += "(value)";
            break;
        case CaseType::True:
            result += "(true)";
            break;
        case CaseType::Else:
            result += "(else)";
            break;
        }
        return result;
    }

  private:
    CaseType caseType_;
};

class CastLoad : public Load {
  public:
    CastLoad(const type_ptr_t &targetType) : Load(LoadType::CAST), targetType_(targetType) {}

    const type_ptr_t &targetType() const { return targetType_; }

    const std::string toString() const override { return "CAST: " + targetType_->toString(); }

  private:
    type_ptr_t targetType_;
};

} // namespace GraphConstructTree

namespace GCT = GraphConstructTree;
