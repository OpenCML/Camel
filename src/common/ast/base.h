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
    Root,
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

class RootLoad : public Load {
  public:
    RootLoad() : Load(LoadType::Root) {}
};

inline std::shared_ptr<RootLoad> root_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<RootLoad>(ptr);
}

class RepeatedLoad : public Load {
  public:
    RepeatedLoad() : Load(LoadType::Repeated) {}
};

class OptionalLoad : public Load {
  public:
    OptionalLoad() : Load(LoadType::Optional) {}
};

} // namespace AbstractSyntaxTree