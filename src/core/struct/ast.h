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

class DataASTNode : public ASTNodeLoad {
  private:
    data_ptr_t data_;
    data_vec_t unrefDataVec_;

  public:
    DataASTNode(data_ptr_t data, data_vec_t &&unrefVec)
        : ASTNodeLoad(ASTNodeType::DATA), data_(data), unrefDataVec_(std::move(unrefVec)) {}
    DataASTNode(data_ptr_t data, data_list_t unrefList = {})
        : ASTNodeLoad(ASTNodeType::DATA), data_(data), unrefDataVec_(unrefList) {}

    bool resolved() const { return unrefDataVec_.empty(); }

    data_ptr_t data() { return data_; }
    data_vec_t &getUnrefData() { return unrefDataVec_; }

    const std::string toString() const override;
};

class TypeASTNode : public ASTNodeLoad {
  private:
    type_ptr_t type_;

  public:
    TypeASTNode(type_ptr_t type) : ASTNodeLoad(ASTNodeType::TYPE), type_(type) {}
    type_ptr_t type() const { return type_; }

    const std::string toString() const override;
};

class FuncASTNode : public ASTNodeLoad {
  private:
  public:
    FuncASTNode() : ASTNodeLoad(ASTNodeType::FUNC) {}

    // const std::string toString() const override;
};

class NRefASTNode : public ASTNodeLoad {
  private:
    std::string ident_;

  public:
    NRefASTNode(const std::string &ident) : ASTNodeLoad(ASTNodeType::NREF), ident_(ident) {}

    const std::string ident() const { return ident_; }

    const std::string toString() const override;
};

class DRefASTNode : public ASTNodeLoad {
  private:
    std::string ident_;

  public:
    DRefASTNode(const std::string &ident) : ASTNodeLoad(ASTNodeType::DREF), ident_(ident) {}

    const std::string ident() const { return ident_; }

    const std::string toString() const override;
};

class AssnASTNode : public ASTNodeLoad {
  public:
    AssnASTNode() : ASTNodeLoad(ASTNodeType::ASSN) {}

    // const std::string toString() const override;
};

class AnnoASTNode : public ASTNodeLoad {
  private:
    std::string annotation_;

  public:
    AnnoASTNode(const std::string &annotation) : ASTNodeLoad(ASTNodeType::ANNO), annotation_(annotation) {}

    // const std::string toString() const override;
};

class LinkASTNode : public ASTNodeLoad {
  public:
    LinkASTNode() : ASTNodeLoad(ASTNodeType::LINK) {}

    // const std::string toString() const override;
};

class WithASTNode : public ASTNodeLoad {
  public:
    WithASTNode() : ASTNodeLoad(ASTNodeType::WITH) {}

    // const std::string toString() const override;
};

class RetnASTNode : public ASTNodeLoad {
  public:
    RetnASTNode() : ASTNodeLoad(ASTNodeType::RETN) {}

    // const std::string toString() const override;
};

class ExecASTNode : public ASTNodeLoad {
  public:
    ExecASTNode() : ASTNodeLoad(ASTNodeType::EXEC) {}

    // const std::string toString() const override;
};