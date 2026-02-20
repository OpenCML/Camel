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
 * Updated: Feb. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <variant>
#include <vector>

#include "camel/common/ref.h"
#include "camel/compile/gct/types.h"
#include "camel/core/data.h"
#include "camel/core/func.h"
#include "camel/utils/assert.h"

namespace GraphConstructTree {

// =============================================================================
// Load：树节点载荷基类
// =============================================================================

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
        tokenEnd_   = end;
    }

    LoadType type() const { return type_; }
    std::pair<size_t, size_t> range() const { return {tokenStart_, tokenEnd_}; }

    virtual const std::string toString() const { return to_string(type_); }
    virtual void visit() { throw std::runtime_error("Load::visit() not implemented"); }
};

// =============================================================================
// 各类 Load 子类
// =============================================================================

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
};

class TypeLoad : public Load {
    Type *dataType_;
    ImplMark implMark_ = ImplMark::Graph;
    std::string uri_;

  public:
    TypeLoad(Type *type, ImplMark impl, const std::string &uri = "")
        : Load(LoadType::TYPE), dataType_(type), implMark_(impl), uri_(uri) {}
    Type *dataType() const { return dataType_; }
    ImplMark implMark() const { return implMark_; }
    const std::string &uri() const { return uri_; }

    const std::string toString() const override;
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
};

class AnnoLoad : public Load {
    std::string annotation_;

  public:
    AnnoLoad(const std::string &annotation) : Load(LoadType::ANNO), annotation_(annotation) {}
};

class LinkLoad : public Load {
    size_t args_;
    std::vector<std::string> kwargs_;

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
            result.pop_back();
            result.pop_back();
            result += "]";
        }
        return result;
    }
};

class WithLoad : public Load {
    size_t args_;
    std::vector<std::string> kwargs_;

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
            result.pop_back();
            result.pop_back();
            result += "]";
        }
        return result;
    }
};

class ExitLoad : public Load {
    ExitType exitType_ = ExitType::Return;

  public:
    ExitLoad(ExitType type = ExitType::Return) : Load(LoadType::EXIT), exitType_(type) {}
    ExitType exitType() const { return exitType_; }

    const std::string toString() const override { return "EXIT: " + to_string(exitType_); }
};

class ExecLoad : public Load {
    bool synced_ = false;

  public:
    ExecLoad(bool sync = false) : Load(LoadType::EXEC), synced_(sync) {}
    bool synced() const { return synced_; }

    const std::string toString() const override { return synced_ ? "SYNC" : "EXEC"; }
};

class ExptLoad : public Load {
    std::vector<Reference> exports_;

  public:
    ExptLoad(const std::vector<Reference> &exports) : Load(LoadType::EXPT), exports_(exports) {}

    const std::string toString() const override {
        std::string result = "EXPT: [";
        for (const auto &exp : exports_) {
            result += exp.toString() + ", ";
        }
        if (!exports_.empty()) {
            result.pop_back();
            result.pop_back();
        }
        result += "]";
        return result;
    }

    const std::vector<Reference> &exports() const { return exports_; }
};

class AccsLoad : public Load {
    std::variant<std::string, size_t> index_;

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
};

class BrchLoad : public Load {
  public:
    BrchLoad() : Load(LoadType::BRCH) {}
};

class CaseLoad : public Load {
  public:
    enum class CaseType {
        Value,
        True,
        Else,
    };

  private:
    CaseType caseType_;

  public:
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
};

class CastLoad : public Load {
    Type *targetType_;

  public:
    CastLoad(Type *targetType) : Load(LoadType::CAST), targetType_(targetType) {}
    Type *targetType() const { return targetType_; }

    const std::string toString() const override { return "CAST: " + targetType_->toString(); }
};

} // namespace GraphConstructTree
