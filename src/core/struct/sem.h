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
#include "functor.h"

class SemanticNode;
using sem_ptr_t = std::shared_ptr<const SemanticNode>;

enum class SemNodeType { DATA, TYPE, FUNC, COPY, NREF, DREF, ANNO, LINK, CALL, RETN };

class SemanticNode {
  protected:
    SemNodeType type_;

  public:
    SemanticNode(SemNodeType type) : type_(type) {}
    virtual ~SemanticNode() = default;

    const std::string typeStr() const;

    virtual const std::string toString() const { return typeStr(); }
    virtual void visit() = 0;
};

class DataNode : public SemanticNode {
  private:
    entity_ptr_t entity_;

  public:
    DataNode(entity_ptr_t entity) : SemanticNode(SemNodeType::DATA), entity_(entity) {}

    const std::string toString() const override;
};

class TypeNode : public SemanticNode {
  private:
    type_ptr_t type_;

  public:
    TypeNode(type_ptr_t type) : SemanticNode(SemNodeType::TYPE), type_(type) {}

    const std::string toString() const override;
};

class FunctorNode : public SemanticNode {
  private:
    std::set<FunctorModifier> modifiers_;

  public:
    FunctorNode(const std::initializer_list<FunctorModifier> &modifiers)
        : SemanticNode(SemNodeType::FUNC), modifiers_(modifiers) {}

    // const std::string toString() const override;
};

class CopyNode : public SemanticNode {
  public:
    CopyNode() : SemanticNode(SemNodeType::COPY) {}

    // const std::string toString() const override;
};

class NewRefNode : public SemanticNode {
  private:
    std::string ident_;

  public:
    NewRefNode(const std::string &ident) : SemanticNode(SemNodeType::NREF), ident_(ident) {}

    const std::string toString() const override { return "NREF: " + ident_; }
};

class DeRefNode : public SemanticNode {
  private:
    std::string ident_;

  public:
    DeRefNode(const std::string &ident) : SemanticNode(SemNodeType::DREF), ident_(ident) {}

    const std::string toString() const override { return "DREF: " + ident_; }
};

class AnnotationNode : public SemanticNode {
  private:
    std::string annotation_;

  public:
    AnnotationNode(const std::string &annotation) : SemanticNode(SemNodeType::ANNO), annotation_(annotation) {}

    // const std::string toString() const override;
};

class LinkNode : public SemanticNode {
  public:
    LinkNode() : SemanticNode(SemNodeType::LINK) {}

    // const std::string toString() const override;
};

class CallNode : public SemanticNode {
  public:
    CallNode() : SemanticNode(SemNodeType::CALL) {}

    // const std::string toString() const override;
};

class ReturnNode : public SemanticNode {
  public:
    ReturnNode() : SemanticNode(SemNodeType::RETN) {}

    // const std::string toString() const override;
};