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

#include <memory>
#include <stdexcept>
#include <string>

#include "common/ref.h"

namespace AbstractSyntaxTree {

enum class ImplMark { Inner, Outer, Graph };

std::string implMarkToString(ImplMark mark);

class Node;
using node_ptr_t = std::shared_ptr<Node>;

class Load;
using load_ptr_t = std::shared_ptr<Load>;

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
        // TODO: add token range check
    }
    const std::string geneCode() const;

    LoadType type() const { return type_; }
    std::pair<size_t, size_t> range() const { return {tokenStart_, tokenEnd_}; }
    const std::string typeStr() const;

    virtual const std::string toString() const { return typeStr(); }
    virtual void visit() { throw std::runtime_error("Load::visit() not implemented"); };
};

class Literal {
  public:
    virtual ~Literal() = default;
    virtual const std::string toString() const = 0;
};

enum class LoadType {
    Module,

    Import,
    Export,

    Stmt,
    Data,
    Type,

    Carrier,
    ParamDecl,
    ParamData,
    KeyTypePair,

    Repeated,
    Optional,
};

class ModuleLoad : public Load {
  public:
    ModuleLoad(const Reference ref) : Load(LoadType::Module), ref_(ref) {}
    const std::string toString() const override { return "Module: " + ref_.toString(); }

  private:
    Reference ref_;
};

inline std::shared_ptr<ModuleLoad> module_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<ModuleLoad>(ptr);
}

class ImportDeclLoad : public Load {
  public:
    ImportDeclLoad(std::string path, std::vector<Reference> refs, Reference as)
        : Load(LoadType::Import), path_(path), refs_(refs), as_(as) {}
    const std::string toString() const override;

  private:
    std::string path_;
    std::vector<Reference> refs_;
    Reference as_;
};

class ExportDeclLoad : public Load {
  public:
    ExportDeclLoad(std::vector<Reference> refs) : Load(LoadType::Export), refs_(refs) {}
    const std::string toString() const override;

  private:
    std::vector<Reference> refs_;
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