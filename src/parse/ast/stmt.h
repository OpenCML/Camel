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

#include "base.h"

namespace AbstractSyntaxTree {

enum class StmtType { Name, Data, Type, Func, Expr, Exit, Block };

enum class UnpackType { Dict, List, Tuple };

enum class ExitType { Yield, Raise, Return, Throw };

class StmtLoad : public Load {
  public:
    StmtLoad(StmtType type_) : Load(LoadType::Stmt), StmtType_(type_) {}

    StmtType stmtType() const { return StmtType_; }
    const std::string geneCode() const override;

  private:
    StmtType StmtType_;
};

class DataDeclLoad : public StmtLoad {
  public:
    DataDeclLoad(bool isVar, UnpackType type, const std::vector<Reference> &refs)
        : StmtLoad(StmtType::Data), isVar_(isVar), type_(type), refs_(refs) {}
    const std::string toString() const override;

    bool isVar() const { return isVar_; }
    UnpackType unpackType() const { return type_; }
    const std::vector<Reference> &refs() const { return refs_; }
    const std::string geneCode() const override;

  private:
    bool isVar_;
    UnpackType type_;
    std::vector<Reference> refs_;
};

class FuncDeclLoad : public StmtLoad {
  public:
    FuncDeclLoad(Reference ref) : StmtLoad(StmtType::Func), ref_(ref) {}

    const Reference &ref() const { return ref_; }

    const std::string toString() const override { return "FuncDecl: " + ref_.toString(); }
    const std::string geneCode() const override;

  private:
    Reference ref_;
};

class TypeDeclLoad : public StmtLoad {
  public:
    TypeDeclLoad(Reference ref, ImplMark impl = ImplMark::Graph, std::string uri = "")
        : StmtLoad(StmtType::Type), ref_(ref), implMark_(impl), uri_(uri) {}

    const Reference &ref() const { return ref_; }
    ImplMark implMark() const { return implMark_; }
    const std::string &uri() const { return uri_; }

    const std::string toString() const override;
    const std::string geneCode() const override;

  private:
    Reference ref_;
    ImplMark implMark_;
    std::string uri_;
};

class NameDeclLoad : public StmtLoad {
  public:
    NameDeclLoad(Reference ref, Reference alias)
        : StmtLoad(StmtType::Name), ref_(ref), alias_(alias) {}
    const std::string toString() const override {
        return "NameDecl: " + ref_.toString() + " as " + alias_.toString();
    }

    const Reference &ref() const { return ref_; }
    const Reference &alias() const { return alias_; }
    const std::string geneCode() const override;

  private:
    Reference ref_;
    Reference alias_;
};

class ExprStmtLoad : public StmtLoad {
  public:
    ExprStmtLoad() : StmtLoad(StmtType::Expr) {}
    const std::string toString() const override { return "ExprStmt"; }
};

class ExitStmtLoad : public StmtLoad {
  public:
    ExitStmtLoad(ExitType type) : StmtLoad(StmtType::Exit), type_(type) {}
    const std::string toString() const override;
    const std::string geneCode() const override;

  private:
    ExitType type_;
};

class StmtBlockLoad : public StmtLoad {
  public:
    StmtBlockLoad(bool sync = false) : StmtLoad(StmtType::Block), synced_(sync) {}

    void sync() { synced_ = true; }
    bool synced() const { return synced_; }

    void wait() { waited_ = true; }
    bool waited() const { return waited_; }

    const std::string toString() const override {
        return std::string("StmtBlock: ") + (synced_ ? "sync" : "") + (waited_ ? " wait" : "");
    }
    const std::string geneCode() const override;

  private:
    bool synced_ = false;
    bool waited_ = false;
};

} // namespace AbstractSyntaxTree