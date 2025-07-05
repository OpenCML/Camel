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

enum class ExitType { Yield, Raise, Return, Throw };

class StmtLoad : public Load {
  public:
    StmtLoad(StmtType type_) : Load(LoadType::Stmt), StmtType_(type_) {}

  private:
    StmtType StmtType_;
};

class DataDeclLoad : public StmtLoad {
  public:
    DataDeclLoad(bool isVar) : StmtLoad(StmtType::Data), isVar_(isVar) {}
    const std::string toString() const override { return isVar_ ? "DataDecl: VAR" : "DataDecl: LET"; }

  private:
    bool isVar_;
};

class FuncDeclLoad : public StmtLoad {
  public:
    FuncDeclLoad(Reference ref) : StmtLoad(StmtType::Func), ref_(ref) {}
    const std::string toString() const override { return "FuncDecl: " + ref_.toString(); }

  private:
    Reference ref_;
};

class TypeDeclLoad : public StmtLoad {
  public:
    TypeDeclLoad(Reference ref, std::string uri, ImplMark impl = ImplMark::Graph)
        : StmtLoad(StmtType::Type), ref_(ref), uri_(uri), implMark_(impl) {}
    const std::string toString() const override;

  private:
    Reference ref_;
    std::string uri_;
    ImplMark implMark_;
};

class NameDeclLoad : public StmtLoad {
  public:
    NameDeclLoad(Reference ref, Reference alias) : StmtLoad(StmtType::Name), ref_(ref), alias_(alias) {}
    const std::string toString() const override { return "NameDecl: " + ref_.toString() + " as " + alias_.toString(); }

  private:
    Reference ref_;
    Reference alias_;
};

class DataDeclLoad : public StmtLoad {
  public:
    DataDeclLoad(Reference ref) : StmtLoad(StmtType::Data), ref_(ref) {}
    const std::string toString() const override { return "DataStmt: " + ref_.toString(); }

  private:
    Reference ref_;
};

class DataExprLoad : public StmtLoad {
  public:
    DataExprLoad() : StmtLoad(StmtType::Expr) {}
    const std::string toString() const override { return "DataExpr"; }
};

class ExitStmtLoad : public StmtLoad {
  public:
    ExitStmtLoad(ExitType type) : StmtLoad(StmtType::Exit), type_(type) {}
    const std::string toString() const override;

  private:
    ExitType type_;
};

class StmtBlockLoad : public StmtLoad {
  public:
    StmtBlockLoad(bool sync = false) : StmtLoad(StmtType::Block), sync_(sync) {}
    void setSync(bool sync) { sync_ = sync; }
    bool isSync() const { return sync_; }

  private:
    bool sync_ = false;
};

} // namespace AbstractSyntaxTree