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
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <variant>
#include <vector>

#include "camel/common/ref.h"
#include "camel/compile/gct/types.h"
#include "camel/core/data.h"
#include "camel/core/func.h"
#include "camel/core/source/ids.h"
#include "camel/utils/assert.h"

namespace camel::compile::gct {

using origin_id_t = camel::source::origin_id_t;
using data_ptr_t  = camel::core::data::data_ptr_t;
using Type        = camel::core::type::Type;

// =============================================================================
// Load: base payload type for tree nodes
// =============================================================================

class Load {
  protected:
    LoadType type_;
    // GCT no longer stores tokens; it holds the origin derived from the AST.
    // This avoids looking back at AST token ranges during diagnostics and GIR
    // construction.
    origin_id_t origin_ = camel::source::kInvalidOriginId;

  public:
    Load(LoadType type) : type_(type) {}
    virtual ~Load() = default;

    void setOrigin(origin_id_t origin) { origin_ = origin; }

    LoadType type() const { return type_; }
    origin_id_t origin() const { return origin_; }

    virtual const std::string toString() const { return to_string(type_); }
    virtual void visit() { throw std::runtime_error("Load::visit() not implemented"); }
};

// =============================================================================
// Load subclasses
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
    /// A scope the lowering introduced (the operands of a composite literal
    /// `{ a: f(), b: g() }`, the names of a declaration `let (p, q) = f()`): not a block of
    /// the program, so its expressions take the ordering of the block they appear in.
    bool operands_ = false;

  public:
    ExecLoad(bool sync = false, bool operands = false)
        : Load(LoadType::EXEC), synced_(sync), operands_(operands) {}
    bool synced() const { return synced_; }
    bool operands() const { return operands_; }

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

/// The reserved name under which the arm of `x ?-> f` finds the value taken out of `x`. It
/// is not an identifier, so no program can write it.
inline constexpr const char *kPassedValueName = "$passed";

class BrchLoad : public Load {
  public:
    /// What selects the arm (design principles 2.9; all are the same structure on the graph).
    ///  - Cond: `if` (a boolean) and `match` (a value compared with the cases, or the tag
    ///    of a `T?` / `T!` with the fixed patterns). Children: condition, CASE...
    ///  - NullThen: `a ?? b`. Children: a, CASE(True)[EXEC b].
    ///  - ErrorThen: `a !! b`. Children: a, CASE(True)[EXEC b].
    ///  - ErrorPass: `x ?-> f`. Children: x, CASE(Else)[EXEC call], where the call takes
    ///    DREF kPassedValueName as its argument.
    enum class Kind { Cond, NullThen, ErrorThen, ErrorPass };

    BrchLoad(Kind kind = Kind::Cond) : Load(LoadType::BRCH), kind_(kind) {}
    Kind kind() const { return kind_; }

    const std::string toString() const override {
        switch (kind_) {
        case Kind::NullThen:
            return "BRCH: ??";
        case Kind::ErrorThen:
            return "BRCH: !!";
        case Kind::ErrorPass:
            return "BRCH: ?->";
        default:
            return "BRCH";
        }
    }

  private:
    Kind kind_;
};

class CaseLoad : public Load {
  public:
    enum class CaseType {
        Value,
        True,
        Else,
        // The fixed patterns over a tagged scrutinee (design principles 2.9). One child, the
        // body. Bind: `case v`, the value taken out of a `T?` / `T!` under the name
        // binding(). Error: `case error e`, the error of a failed `T!` under that name.
        Bind,
        Error,
    };

  private:
    CaseType caseType_;
    std::string binding_;

  public:
    CaseLoad(CaseType type, std::string binding = {})
        : Load(LoadType::CASE), caseType_(type), binding_(std::move(binding)) {}
    CaseType caseType() const { return caseType_; }
    const std::string &binding() const { return binding_; }

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
        case CaseType::Bind:
            result += "(bind " + binding_ + ")";
            break;
        case CaseType::Error:
            result += "(error " + binding_ + ")";
            break;
        }
        return result;
    }
};

class CastLoad : public Load {
    Type *targetType_;
    bool annotation_;

  public:
    /// `annotation`: the conversion is the type written on a declaration (`let x: T = 0`),
    /// not an `as` expression. It only matters for a literal converted to a type written
    /// with a type variable, which is converted when the generic function is instantiated
    /// by the rule the GCT applies to the same literal and a concrete type.
    CastLoad(Type *targetType, bool annotation = false)
        : Load(LoadType::CAST), targetType_(targetType), annotation_(annotation) {}
    Type *targetType() const { return targetType_; }
    bool annotation() const { return annotation_; }

    const std::string toString() const override { return "CAST: " + targetType_->toString(); }
};

} // namespace camel::compile::gct
