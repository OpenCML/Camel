/**
 * Copyright (c) 2024 Beijing Jiaotong University
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
 * Created: May. 5, 2024
 * Updated: Oct. 08, 2024
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "data.h"
#include "entity.h"

namespace AbstractSyntaxTree {

class Node;
using node_ptr_t = std::shared_ptr<Node>;

class Load;
using load_ptr_t = std::shared_ptr<Load>;

enum class NodeType { DATA, VARI, TYPE, FUNC, NREF, DREF, WAIT, ANNO, LINK, WITH, RETN, EXEC, FROM };

class Load {
  protected:
    NodeType type_;

  public:
    Load(NodeType type) : type_(type) {}
    virtual ~Load() = default;

    NodeType type() const { return type_; }
    const std::string typeStr() const;

    virtual const std::string toString() const { return typeStr(); }
    virtual void visit() { throw std::runtime_error("Load::visit() not implemented"); };
};

class DataLoad : public Load {
    data_ptr_t data_;

  public:
    DataLoad(data_ptr_t data) : Load(NodeType::DATA), data_(data) {}

    data_ptr_t data() { return data_; }

    const std::string toString() const override;
};

inline std::shared_ptr<DataLoad> data_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<DataLoad>(ptr);
}

class VariLoad : public Load {
  public:
    VariLoad() : Load(NodeType::VARI) {}

    // const std::string toString() const override;
};

inline std::shared_ptr<VariLoad> vari_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<VariLoad>(ptr);
}

class TypeLoad : public Load {
    type_ptr_t type_;

  public:
    TypeLoad(type_ptr_t type) : Load(NodeType::TYPE), type_(type) {}
    type_ptr_t type() const { return type_; }

    const std::string toString() const override;
};

inline std::shared_ptr<TypeLoad> type_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<TypeLoad>(ptr);
}

class FuncLoad : public Load {
  public:
    FuncLoad() : Load(NodeType::FUNC) {}

    // const std::string toString() const override;
};

inline std::shared_ptr<FuncLoad> func_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<FuncLoad>(ptr);
}

class NRefLoad : public Load {
    std::string ident_;

  public:
    NRefLoad(const std::string &ident) : Load(NodeType::NREF), ident_(ident) {}

    const std::string ident() const { return ident_; }

    const std::string toString() const override;
};

inline std::shared_ptr<NRefLoad> nref_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<NRefLoad>(ptr);
}

class DRefLoad : public Load {
    std::string ident_;

  public:
    DRefLoad(const std::string &ident) : Load(NodeType::DREF), ident_(ident) {}

    const std::string ident() const { return ident_; }

    const std::string toString() const override;
};

inline std::shared_ptr<DRefLoad> dref_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<DRefLoad>(ptr);
}

class WaitLoad : public Load {
    std::vector<std::string> idents_;

  public:
    WaitLoad() : Load(NodeType::WAIT) {}

    void wait(const std::string &ident) { idents_.push_back(ident); }
    const std::vector<std::string> &waited() const { return idents_; }

    const std::string toString() const override;
};

inline std::shared_ptr<WaitLoad> wait_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<WaitLoad>(ptr);
}

class AnnoLoad : public Load {
    std::string annotation_;

  public:
    AnnoLoad(const std::string &annotation) : Load(NodeType::ANNO), annotation_(annotation) {}

    // const std::string toString() const override;
};

inline std::shared_ptr<AnnoLoad> anno_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<AnnoLoad>(ptr);
}

class LinkLoad : public Load {
  public:
    LinkLoad() : Load(NodeType::LINK) {}

    // const std::string toString() const override;
};

inline std::shared_ptr<LinkLoad> link_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<LinkLoad>(ptr);
}

class WithLoad : public Load {
  public:
    WithLoad() : Load(NodeType::WITH) {}

    // const std::string toString() const override;
};

inline std::shared_ptr<WithLoad> with_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<WithLoad>(ptr);
}

class RetnLoad : public Load {
  public:
    RetnLoad() : Load(NodeType::RETN) {}

    // const std::string toString() const override;
};

inline std::shared_ptr<RetnLoad> retn_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<RetnLoad>(ptr);
}

class ExecLoad : public Load {
  public:
    ExecLoad() : Load(NodeType::EXEC) {}

    // const std::string toString() const override;
};

inline std::shared_ptr<ExecLoad> exec_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<ExecLoad>(ptr);
}

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

inline std::shared_ptr<FromLoad> from_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<FromLoad>(ptr);
}

} // namespace AbstractSyntaxTree

namespace ast = AbstractSyntaxTree;
