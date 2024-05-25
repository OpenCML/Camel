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

class StaticStmt;
class StmtList;
using stmt_ptr_t = std::shared_ptr<const StaticStmt>;
using stmt_list_ptr_t = std::shared_ptr<const StmtList>;

enum class StmtType { DATA, TYPE, FUNC, COPY, NREF, PACK, UNPK, ANNO, LINK, WITH };

std::string stmtTypeToString(StmtType type);

class StaticStmt {
  protected:
    StmtType type_;

  public:
    StaticStmt(StmtType type) : type_(type) {}
    virtual ~StaticStmt() = default;

    virtual void execute() = 0;
};

class DataStmt : public StaticStmt {
  private:
    entity_ptr_t entity_;

  public:
    DataStmt(entity_ptr_t entity) : StaticStmt(StmtType::DATA), entity_(entity) {}
};

class TypeStmt : public StaticStmt {
  private:
    type_ptr_t type_;

  public:
    TypeStmt(type_ptr_t type) : StaticStmt(StmtType::TYPE), type_(type) {}
};

class FunctorStmt : public StaticStmt {
  private:
    std::string name_;
    stmt_list_ptr_t body_;

  public:
    FunctorStmt(const std::string &name, stmt_list_ptr_t body) : StaticStmt(StmtType::FUNC), name_(name), body_(body) {}
};

class CopyStmt : public StaticStmt {
  private:
    entity_ptr_t target_;
    entity_ptr_t source_;

  public:
    CopyStmt(entity_ptr_t target, entity_ptr_t source) : StaticStmt(StmtType::COPY), target_(target), source_(source) {}
};

class NewRefStmt : public StaticStmt {
  private:
    entity_ptr_t target_;
    entity_ptr_t source_;

  public:
    NewRefStmt(entity_ptr_t target, entity_ptr_t source)
        : StaticStmt(StmtType::NREF), target_(target), source_(source) {}
};

class PackStmt : public StaticStmt {
  private:
    entity_ptr_t target_;
    entity_ptr_t source_;

  public:
    PackStmt(entity_ptr_t target, entity_ptr_t source) : StaticStmt(StmtType::PACK), target_(target), source_(source) {}
};

class UnpackStmt : public StaticStmt {
  private:
    entity_ptr_t target_;
    entity_ptr_t source_;

  public:
    UnpackStmt(entity_ptr_t target, entity_ptr_t source)
        : StaticStmt(StmtType::UNPK), target_(target), source_(source) {}
};

class AnnotationStmt : public StaticStmt {
  private:
    std::string annotation_;

  public:
    AnnotationStmt(const std::string &annotation) : StaticStmt(StmtType::ANNO), annotation_(annotation) {}
};

class LinkStmt : public StaticStmt {
  private:
    entity_ptr_t target_;
    entity_ptr_t source_;

  public:
    LinkStmt(entity_ptr_t target, entity_ptr_t source) : StaticStmt(StmtType::LINK), target_(target), source_(source) {}
};

class WithStmt : public StaticStmt {
  private:
    entity_ptr_t target_;
    entity_ptr_t source_;

  public:
    WithStmt(entity_ptr_t target, entity_ptr_t source) : StaticStmt(StmtType::WITH), target_(target), source_(source) {}
};