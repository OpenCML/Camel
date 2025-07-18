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

#include "common/func.h"
#include "common/impl.h"
#include "common/literal.h"
#include "common/ref.h"
#include "common/tree.h"

namespace AbstractSyntaxTree {

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

std::string to_string(LoadType type);

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
    std::pair<size_t, size_t> tokenRange() const { return tokenRange_; }
    const std::string typeStr() const { return to_string(type_); }

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

    template <typename T> node_ptr_t atAs(size_t index) const {
        // safe check for index and type
        assert(index < children_.size() && "Index out of bounds");
        assert(children_.at(index) != nullptr && "Child node is null");
        if (std::dynamic_pointer_cast<T>(children_.at(index)->load()) == nullptr) {
            std::cerr << "Error: Node type cast filed. Expected: " << typeid(T).name()
                      << ", Actual: " << typeid(*children_.at(index)->load()).name() << std::endl;
            throw std::runtime_error("Node type mismatch");
        }
        return children_.at(index);
    }
    template <typename T> node_ptr_t optAtAs(size_t index) const {
        const auto &opt = at(index);
        assert(opt->type() == LoadType::Optional && "Expected OptionalLoad type");
        if (opt->load()->type() == LoadType::Optional && opt->empty()) {
            return nullptr; // return null if it's an empty optional
        }
        return opt->atAs<T>(0);
    }

    template <typename LoadType> std::shared_ptr<LoadType> loadAs() {
        if (std::dynamic_pointer_cast<LoadType>(load_) == nullptr) {
            // TODO: delete this line in production code
            std::cerr << "Error: Load type cast failed. Expected: " << typeid(LoadType).name()
                      << ", Actual: " << typeid(*load_).name() << std::endl;
            throw std::runtime_error("Load type mismatch");
        }
        return std::dynamic_pointer_cast<LoadType>(load_);
    }
    template <typename LoadType> const std::shared_ptr<LoadType> loadAs() const {
        assert(std::dynamic_pointer_cast<LoadType>(load_) && "Load type does not match requested type");
        return std::dynamic_pointer_cast<LoadType>(load_);
    }
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

    const Reference &ref() const { return ref_; }

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
    bool isVar() const { return isVar_; }

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