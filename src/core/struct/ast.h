/**
 * Copyright (c) 2022 Beijing Jiaotong University
 * PhotLab is licensed under [Open Source License].
 * You can use this software according to the terms and conditions of the [Open
 * Source License]. You may obtain a copy of [Open Source License] at:
 * [https://open.source.license/]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the [Open Source License] for more details.
 *
 * Author: Zhenjie Wei
 * Created: May. 5, 2024
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "entity.h"
#include "data.h"

class ASTNode;
using ast_ptr_t = std::shared_ptr<ASTNode>;

class ASTNodeLoad;
using ast_load_ptr_t = std::shared_ptr<ASTNodeLoad>;

enum class ASTNodeType { DATA, TYPE, FUNC, NREF, DREF, ASSN, ANNO, LINK, WITH, RETN, EXEC };

class ASTNodeLoad {
  protected:
    ASTNodeType type_;

  public:
    ASTNodeLoad(ASTNodeType type) : type_(type) {}
    virtual ~ASTNodeLoad() = default;

    ASTNodeType type() const { return type_; }
    const std::string typeStr() const;

    virtual const std::string toString() const { return typeStr(); }
    virtual void visit() { throw std::runtime_error("ASTNodeLoad::visit() not implemented"); };
};

class DataASTLoad : public ASTNodeLoad {
  private:
    data_ptr_t data_;
    data_vec_t unrefDataVec_;

  public:
    DataASTLoad(data_ptr_t data, data_vec_t &&unrefVec)
        : ASTNodeLoad(ASTNodeType::DATA), data_(data), unrefDataVec_(std::move(unrefVec)) {}
    DataASTLoad(data_ptr_t data, data_list_t unrefList = {})
        : ASTNodeLoad(ASTNodeType::DATA), data_(data), unrefDataVec_(unrefList) {}

    bool resolved() const { return unrefDataVec_.empty(); }

    data_ptr_t data() { return data_; }
    data_vec_t &getUnrefData() { return unrefDataVec_; }

    const std::string toString() const override;
};

inline std::shared_ptr<DataASTLoad> data_ast_load_ptr_cast(const ast_load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<DataASTLoad>(ptr);
}

class TypeASTLoad : public ASTNodeLoad {
  private:
    type_ptr_t type_;

  public:
    TypeASTLoad(type_ptr_t type) : ASTNodeLoad(ASTNodeType::TYPE), type_(type) {}
    type_ptr_t type() const { return type_; }

    const std::string toString() const override;
};

inline std::shared_ptr<TypeASTLoad> type_ast_load_ptr_cast(const ast_load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<TypeASTLoad>(ptr);
}

class FuncASTLoad : public ASTNodeLoad {
  private:
  public:
    FuncASTLoad() : ASTNodeLoad(ASTNodeType::FUNC) {}

    // const std::string toString() const override;
};

inline std::shared_ptr<FuncASTLoad> func_ast_load_ptr_cast(const ast_load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<FuncASTLoad>(ptr);
}

class NRefASTLoad : public ASTNodeLoad {
  private:
    std::string ident_;

  public:
    NRefASTLoad(const std::string &ident) : ASTNodeLoad(ASTNodeType::NREF), ident_(ident) {}

    const std::string ident() const { return ident_; }

    const std::string toString() const override;
};

inline std::shared_ptr<NRefASTLoad> nref_ast_load_ptr_cast(const ast_load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<NRefASTLoad>(ptr);
}

class DRefASTLoad : public ASTNodeLoad {
  private:
    std::string ident_;

  public:
    DRefASTLoad(const std::string &ident) : ASTNodeLoad(ASTNodeType::DREF), ident_(ident) {}

    const std::string ident() const { return ident_; }

    const std::string toString() const override;
};

inline std::shared_ptr<DRefASTLoad> dref_ast_load_ptr_cast(const ast_load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<DRefASTLoad>(ptr);
}

class AssnASTLoad : public ASTNodeLoad {
  public:
    AssnASTLoad() : ASTNodeLoad(ASTNodeType::ASSN) {}

    // const std::string toString() const override;
};

inline std::shared_ptr<AssnASTLoad> assn_ast_load_ptr_cast(const ast_load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<AssnASTLoad>(ptr);
}

class AnnoASTLoad : public ASTNodeLoad {
  private:
    std::string annotation_;

  public:
    AnnoASTLoad(const std::string &annotation) : ASTNodeLoad(ASTNodeType::ANNO), annotation_(annotation) {}

    // const std::string toString() const override;
};

inline std::shared_ptr<AnnoASTLoad> anno_ast_load_ptr_cast(const ast_load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<AnnoASTLoad>(ptr);
}

class LinkASTLoad : public ASTNodeLoad {
  public:
    LinkASTLoad() : ASTNodeLoad(ASTNodeType::LINK) {}

    // const std::string toString() const override;
};

inline std::shared_ptr<LinkASTLoad> link_ast_load_ptr_cast(const ast_load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<LinkASTLoad>(ptr);
}

class WithASTLoad : public ASTNodeLoad {
  public:
    WithASTLoad() : ASTNodeLoad(ASTNodeType::WITH) {}

    // const std::string toString() const override;
};

inline std::shared_ptr<WithASTLoad> with_ast_load_ptr_cast(const ast_load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<WithASTLoad>(ptr);
}

class RetnASTLoad : public ASTNodeLoad {
  public:
    RetnASTLoad() : ASTNodeLoad(ASTNodeType::RETN) {}

    // const std::string toString() const override;
};

inline std::shared_ptr<RetnASTLoad> retn_ast_load_ptr_cast(const ast_load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<RetnASTLoad>(ptr);
}

class ExecASTLoad : public ASTNodeLoad {
  public:
    ExecASTLoad() : ASTNodeLoad(ASTNodeType::EXEC) {}

    // const std::string toString() const override;
};

inline std::shared_ptr<ExecASTLoad> exec_ast_load_ptr_cast(const ast_load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<ExecASTLoad>(ptr);
}