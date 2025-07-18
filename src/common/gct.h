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
 * Updated: Mar. 10, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "common/ref.h"
#include "data.h"
#include "entity.h"
#include <cassert>

namespace GraphConstructTree {

class Node;
using node_ptr_t = std::shared_ptr<Node>;

class Load;
using load_ptr_t = std::shared_ptr<Load>;

enum class NodeType {
    DATA, //
    VARI,
    TYPE,
    DECL,
    FUNC,
    NREF,
    DREF,
    WAIT,
    ANNO,
    LINK,
    WITH,
    BIND,
    EXIT,
    EXEC,
    FROM,
    ACCS,
    BRCH,
};

std::string to_string(NodeType type);

class Load {
  protected:
    NodeType type_;
    size_t tokenStart_;
    size_t tokenEnd_;

  public:
    Load(NodeType type) : type_(type) {}
    virtual ~Load() = default;

    void setToken(size_t start, size_t end) {
        tokenStart_ = start;
        tokenEnd_ = end;
    }

    NodeType type() const { return type_; }
    std::pair<size_t, size_t> range() const { return {tokenStart_, tokenEnd_}; }

    virtual const std::string toString() const { return to_string(type_); }
    virtual void visit() { throw std::runtime_error("Load::visit() not implemented"); };
};

class DataLoad : public Load {
    data_ptr_t data_;

  public:
    DataLoad(data_ptr_t data) : Load(NodeType::DATA), data_(data) {
        assert(data != nullptr && "DataLoad cannot be constructed with a null data pointer");
    }

    data_ptr_t data() { return data_; }

    const std::string toString() const override;
};

class VariLoad : public Load {
  public:
    VariLoad() : Load(NodeType::VARI) {}

    // const std::string toString() const override;
};

class TypeLoad : public Load {
    type_ptr_t dataType_;

  public:
    TypeLoad(type_ptr_t type, ImplMark impl, const std::string &uri)
        : Load(NodeType::TYPE), dataType_(type), implMark_(impl), uri_(uri) {}
    type_ptr_t dataType() const { return dataType_; }

    ImplMark implMark() const { return implMark_; }
    const std::string &uri() const { return uri_; }

    const std::string toString() const override;

  private:
    ImplMark implMark_ = ImplMark::Graph;
    std::string uri_;
};

class DeclLoad : public Load {
    func_type_ptr_t funcType_;

  public:
    DeclLoad(func_type_ptr_t type) : Load(NodeType::DECL), funcType_(type) {}
    func_type_ptr_t funcType() const { return funcType_; }

    const std::string toString() const override;
};

class FuncLoad : public Load {
    func_type_ptr_t funcType_;

  public:
    FuncLoad(func_type_ptr_t type) : Load(NodeType::FUNC), funcType_(type) {}
    func_type_ptr_t funcType() const { return funcType_; }

    // const std::string toString() const override;
};

class NRefLoad : public Load {
    Reference ref_;

  public:
    NRefLoad(const Reference &ref) : Load(NodeType::NREF), ref_(ref) {}
    NRefLoad(const std::string &str) : Load(NodeType::NREF), ref_(str) {}

    const Reference ref() const { return ref_; }

    const std::string toString() const override;
};

class DRefLoad : public Load {
    Reference ref_;

  public:
    DRefLoad(const Reference &ref) : Load(NodeType::DREF), ref_(ref) {}
    DRefLoad(const std::string &str) : Load(NodeType::DREF), ref_(str) {}

    const Reference ref() const { return ref_; }

    const std::string toString() const override;
};

class WaitLoad : public Load {
    std::vector<std::string> idents_;

  public:
    WaitLoad() : Load(NodeType::WAIT) {}

    void wait(const std::string &ident) { idents_.push_back(ident); }
    const std::vector<std::string> &waited() const { return idents_; }

    const std::string toString() const override;
};

class AnnoLoad : public Load {
    std::string annotation_;

  public:
    AnnoLoad(const std::string &annotation) : Load(NodeType::ANNO), annotation_(annotation) {}

    // const std::string toString() const override;
};

class LinkLoad : public Load {
  public:
    LinkLoad(size_t args = 0) : Load(NodeType::LINK), args_(args) {}

    void setArgs(size_t args) { args_ = args; }
    void addKwarg(const std::string &kwarg) { kwargs_.push_back(kwarg); }

    const std::string toString() const override {
        std::string result = "LINK: args=" + std::to_string(args_);
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
    WithLoad(size_t args = 0) : Load(NodeType::WITH), args_(args) {}

    void setArgs(size_t args) { args_ = args; }
    void addKwarg(const std::string &kwarg) { kwargs_.push_back(kwarg); }

    const std::string toString() const override {
        std::string result = "WITH: args=" + std::to_string(args_);
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

class BindLoad : public Load {
  public:
    BindLoad() : Load(NodeType::BIND) {}

    // const std::string toString() const override;
};

class RetnLoad : public Load {
  public:
    RetnLoad() : Load(NodeType::EXIT) {}

    // const std::string toString() const override;
};

class ExitLoad : public Load {
  public:
    ExitLoad() : Load(NodeType::EXIT) {}
};

class ExecLoad : public Load {
  public:
    ExecLoad() : Load(NodeType::EXEC) {}

    // const std::string toString() const override;
};

class FromLoad : public Load {
    std::string path_;
    std::vector<std::string> idents_; // if empty, load all(*)

  public:
    FromLoad(std::string &path, std::vector<std::string> &idents)
        : Load(NodeType::FROM), path_(path), idents_(idents) {}

    const std::string path() const { return path_; }
    const std::vector<std::string> &idents() const { return idents_; }

    const std::string toString() const override;
};

class AccsLoad : public Load {
  public:
    AccsLoad(const std::string &index) : Load(NodeType::ACCS), index_(index) {}

    const std::string toString() const override { return "ACCS: " + index_; }

    const std::string &index() const { return index_; }

  private:
    std::string index_;
};

class BrchLoad : public Load {
  public:
    BrchLoad() : Load(NodeType::BRCH) {}

    // const std::string toString() const override;
};

} // namespace GraphConstructTree

namespace GCT = GraphConstructTree;
