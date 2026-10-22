// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include <stdexcept>
#include <string>
#include <vector>

#include "base.h"
#include "camel/common/ref.h"

namespace camel::parse::ast {

class ParamDeclLoad : public Load {
  public:
    ParamDeclLoad(const Reference &ref, bool isVar = false)
        : Load(LoadType::Type), ref_(ref), isVar_(isVar) {}
    const std::string toString() const override {
        return "ParamDecl: " + (isVar_ ? std::string("var ") : "") + ref_.toString();
    }
    const std::string geneCode() const override;

  private:
    Reference ref_;
    bool isVar_;
};

class ParamDataLoad : public Load {
  public:
    ParamDataLoad(const Reference &ref) : Load(LoadType::NamedData), ref_(ref) {}
    const std::string toString() const override { return "ParamData: " + ref_.toString(); }
    const std::string geneCode() const override;

  private:
    Reference ref_;
};

} // namespace camel::parse::ast
