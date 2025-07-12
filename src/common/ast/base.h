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
 * See the the MIT license for more details
 *
 * Author: Zhenjie Wei
 * Created: Jul. 03, 2025
 * Updated: Jul. 03, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <cassert>
#include <memory>
#include <stdexcept>
#include <string>

#include "common/ref.h"
#include "common/tree.h"

namespace AbstractSyntaxTree {

enum class ImplMark { Inner, Outer, Graph };

enum class LoadType {
    Module,

    Import,
    Export,

    Stmt,
    Data,
    Type,

    NamedData,
    NamedType,
    NamedPair,

    Repeated,
    Optional,
};

std::string implMarkToString(ImplMark mark);

std::string loadTypeToString(LoadType type);

class Node;
using node_ptr_t = std::shared_ptr<Node>;

class Load;
using load_ptr_t = std::shared_ptr<Load>;

class Load {
  public:
    Load(LoadType type) : type_(type) {}
    virtual ~Load() = default;

    void setTokenRange(size_t start, size_t end) {
        tokenRange_.first = start;
        tokenRange_.second = end;
    }
    void setTokenRange(std::pair<size_t, size_t> range) { tokenRange_ = range; }
    const std::string geneCode() const;

    LoadType type() const { return type_; }
    std::pair<size_t, size_t> range() const { return tokenRange_; }
    const std::string typeStr() const { return loadTypeToString(type_); }

    virtual const std::string toString() const { return typeStr(); }
    virtual void visit() { throw std::runtime_error("Load::visit() not implemented"); };

  protected:
    LoadType type_;
    std::pair<size_t, size_t> tokenRange_ = {0, 0};
};

class Node : public AbstractTreeNode<load_ptr_t, Node> {
  public:
    Node(load_ptr_t load) : AbstractTreeNode(load) {}
    virtual ~Node() = default;

    LoadType type() const { return load_->type(); }
    std::string toString() const { return load_->toString(); }

    template <typename LoadType> node_ptr_t atAs(size_t index) const {
        // safe check for index and type
        assert(index < children_.size() && "Index out of bounds");
        assert(children_.at(index) != nullptr && "Child node is null");
        assert(typeid(children_.at(index)->load()) == typeid(LoadType) &&
               "Child node type does not match requested type");
        return children_.at(index);
    }

    template <typename LoadType> std::shared_ptr<LoadType> loadAs() {
        assert(typeid(load_) == typeid(LoadType) && "Load type does not match requested type");
        return std::dynamic_pointer_cast<LoadType>(load_);
    }
    template <typename LoadType> const std::shared_ptr<LoadType> loadAs() const {
        assert(typeid(load_) == typeid(LoadType) && "Load type does not match requested type");
        return std::dynamic_pointer_cast<LoadType>(load_);
    }
};

enum class LiteralType {
    String,
    FString,
    Integer,
    Real,
    Boolean,
    Null,
};

class Literal {
  public:
    Literal(LiteralType type, const std::string &data) : type_(type), data_(data) {}
    ~Literal() = default;
    const std::string toString() const { return data_; }

    LiteralType type() const { return type_; }
    const std::string &data() const { return data_; }

  private:
    LiteralType type_;
    std::string data_;
};

class ModuleLoad : public Load {
  public:
    ModuleLoad() : Load(LoadType::Module) {}
    const std::string toString() const override { return "Module: " + ref_.toString(); }

    void setRef(const Reference &ref) { ref_ = ref; }
    const Reference &getRef() const {
        if (ref_.isNull()) {
            throw std::runtime_error("ModuleLoad: Reference is not set");
        }
        return ref_;
    }

  private:
    Reference ref_;
};

class ImportLoad : public Load {
  public:
    ImportLoad() : Load(LoadType::Import) {}
    const std::string toString() const override;

    void setPath(const std::string &path) { path_ = path; }
    const std::string &getPath() const {
        if (path_.empty()) {
            throw std::runtime_error("ImportLoad: Path is not set");
        }
        return path_;
    }

    void setRefs(const std::vector<Reference> &refs) { refs_ = refs; }
    std::vector<Reference> &getRefs() {
        if (refs_.empty()) {
            throw std::runtime_error("ImportLoad: References are not set");
        }
        return refs_;
    }

    void setAs(const Reference &as) { as_ = as; }
    const Reference &getAs() const {
        if (as_.isNull()) {
            throw std::runtime_error("ImportLoad: 'as' Reference is not set");
        }
        return as_;
    }

  private:
    std::string path_;
    std::vector<Reference> refs_;
    Reference as_;
};

class ExportLoad : public Load {
  public:
    ExportLoad() : Load(LoadType::Export) {}
    const std::string toString() const override;

    void setRefs(const std::vector<Reference> &refs) { refs_ = refs; }
    std::vector<Reference> &getRefs() {
        if (refs_.empty()) {
            throw std::runtime_error("ExportLoad: References are not set");
        }
        return refs_;
    }
    bool isEmpty() const { return refs_.empty(); }

    void addRef(const Reference &ref) {
        if (ref.isNull()) {
            throw std::runtime_error("ExportLoad: Cannot add null Reference");
        }
        refs_.push_back(ref);
    }

  private:
    std::vector<Reference> refs_;
};

class NamedDataLoad : public Load {
  public:
    NamedDataLoad(const Reference &ref) : Load(LoadType::NamedData), ref_(ref) {}
    const std::string toString() const override { return "NamedData: " + ref_.toString(); }

    const Reference &getRef() const { return ref_; }

  private:
    Reference ref_;
};

class NamedTypeLoad : public Load {
  public:
    NamedTypeLoad(const Reference &ref, bool isVar = false) : Load(LoadType::NamedType), ref_(ref), isVar_(isVar) {}
    const std::string toString() const override {
        return "NamedType: " + (isVar_ ? std::string("var ") : "") + ref_.toString();
    }

    const Reference &getRef() const { return ref_; }

  private:
    Reference ref_;
    bool isVar_;
};

class NamedPairLoad : public Load {
  public:
    NamedPairLoad(const Reference &ref, bool isVar = false) : Load(LoadType::NamedType), ref_(ref), isVar_(isVar) {}
    const std::string toString() const override {
        return "NamedPair: " + (isVar_ ? std::string("var ") : "") + ref_.toString();
    }
    const Reference &getRef() const { return ref_; }

  private:
    Reference ref_;
    bool isVar_;
};

class RepeatedLoad : public Load {
  public:
    RepeatedLoad(const std::string &nodeName) : Load(LoadType::Repeated), nodeName_(nodeName) {}
    const std::string toString() const override { return nodeName_ + "*"; }

  private:
    std::string nodeName_;
};

class OptionalLoad : public Load {
  public:
    OptionalLoad(const std::string &nodeName) : Load(LoadType::Optional), nodeName_(nodeName) {}
    const std::string toString() const override { return nodeName_ + "?"; }

  private:
    std::string nodeName_;
};

} // namespace AbstractSyntaxTree