// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/parse/ast/other.h"
#include "camel/utils/str.h"

namespace camel::parse::ast {

const std::string ParamDeclLoad::geneCode() const {
    return (isVar_ ? std::string("var ") : "") + ref_.toString();
}

const std::string ParamDataLoad::geneCode() const { return ref_.toString(); }

} // namespace camel::parse::ast
