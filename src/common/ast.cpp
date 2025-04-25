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
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: May. 05, 2024
 * Updated: Apr. 12, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include <iterator>

#include "ast.h"
#include "utils/log.h"

using namespace std;
using namespace AST;

const string Load::typeStr() const {
    switch (type_) {
    case NodeType::Program:
        return "Program";
    case NodeType::Decl:
        return "Decl";
    case NodeType::ModuleDecl:
        return "ModuleDecl";
    case NodeType::ImportDecl:
        return "ImportDecl";
    case NodeType::ExportDecl:
        return "ExportDecl";
    case NodeType::LetDecl:
        return "LetDecl";
    case NodeType::UseDecl:
        return "UseDecl";
    case NodeType::FuncDecl:
        return "FuncDecl";
    case NodeType::TypeDecl:
        return "TypeDecl";
    case NodeType::EnumDecl:
        return "EnumDecl";
    case NodeType::Stmt:
        return "Stmt";
    case NodeType::StmtList:
        return "StmtList";
    case NodeType::StmtBlock:
        return "StmtBlock";
    case NodeType::RetStmt:
        return "RetStmt";
    case NodeType::WaitStmt:
        return "WaitStmt";
    case NodeType::LambdaExpr:
        return "LambdaExpr";
    case NodeType::KeyTypePair:
        return "KeyTypePair";
    case NodeType::KeyValuePair:
        return "KeyValuePair";
    case NodeType::KeyParamPair:
        return "KeyParamPair";
    case NodeType::IdentDefList:
        return "IdentDefList";
    case NodeType::ValueList:
        return "ValueList";
    case NodeType::IndexValues:
        return "IndexValues";
    case NodeType::PairedValues:
        return "PairedValues";
    case NodeType::PairedParams:
        return "PairedParams";
    case NodeType::ArgumentList:
        return "ArgumentList";
    case NodeType::MemberAccess:
        return "MemberAccess";
    case NodeType::DataExpr:
        return "DataExpr";
    case NodeType::IdentList:
        return "IdentList";
    case NodeType::DataList:
        return "DataList";
    case NodeType::CtrlExpr:
        return "CtrlExpr";
    case NodeType::IfLoad:
        return "IfLoad";
    case NodeType::CatchClause:
        return "CatchClause";
    case NodeType::BlockExpr:
        return "BlockExpr";
    case NodeType::WaitExpr:
        return "WaitExpr";
    case NodeType::Wildcard:
        return "Wildcard";
    case NodeType::MatchCase:
        return "MatchCase";
    case NodeType::AssignExpr:
        return "AssignExpr";
    case NodeType::LogicalOrExpr:
        return "LogicalOrExpr";
    case NodeType::LogicalAndExpr:
        return "LogicalAndExpr";
    case NodeType::EqualityExpr:
        return "EqualityExpr";
    case NodeType::RelationalExpr:
        return "RelationalExpr";
    case NodeType::AdditiveExpr:
        return "AdditiveExpr";
    case NodeType::MultiplicativeExpr:
        return "MultiplicativeExpr";
    case NodeType::NullableExpr:
        return "NullableExpr";
    case NodeType::UnaryExpr:
        return "UnaryExpr";
    case NodeType::LinkExpr:
        return "LinkExpr";
    case NodeType::BindExpr:
        return "BindExpr";
    case NodeType::WithExpr:
        return "WithExpr";
    case NodeType::AnnoExpr:
        return "AnnoExpr";
    case NodeType::DictExpr:
        return "DictExpr";
    case NodeType::DictType:
        return "DictType";
    case NodeType::ListExpr:
        return "ListExpr";
    case NodeType::Literal:
        return "Literal";
    case NodeType::TypeExpr:
        return "TypeExpr";
    case NodeType::UnionType:
        return "UnionType";
    case NodeType::UnionUnit:
        return "UnionUnit";
    case NodeType::ListType:
        return "ListType";
    case NodeType::ArgsType:
        return "ArgsType";
    case NodeType::PrimaryType:
        return "PrimaryType";
    case NodeType::DictExprType:
        return "DictExprType";
    case NodeType::TypeList:
        return "TypeList";
    case NodeType::LambdaType:
        return "LambdaType";
    case NodeType::IdentDef:
        return "IdentDef";
    case NodeType::IdentRef:
        return "IdentRef";
    default:
        return "Unknown";
    }
}

const string ModuleDecl::toString() const {
    stringstream ss;
    ss << "ModuleDecl " << "ident: " << ident_;
    return ss.str();
}

const string ImportDecl::toString() const {
    stringstream ss;
    ss << "ImportDecl ";
    if (idents_.empty()) {
        ss << "path: " << path_;
    } else {
        if (idents_.size() == 1) {
            ss << "ident: " << idents_[0];
        } else {
            ss << "idents: ";
            for (auto ident : idents_) {
                ss << ident << " ";
            }
        }
        ss << ",path: " << path_;
    }
    return ss.str();
}

const string ExportDecl::toString() const {
    stringstream ss;
    ss << "ExportDecl ";
    if (!idents_.empty()) {
        ss << "idents: ";
        for (auto ident : idents_) {
            ss << ident << " ";
        }
    }
    return ss.str();
}

const string LambdaExpr::toString() const {
    stringstream ss;
    ss << "LambdaExpr ";
    if (!modifiers_.empty()) {
        ss << "modifiers: ";
        for (auto modifier : modifiers_) {
            if (modifier == Modifier::ATOMIC)
                ss << "ATOMIC ";
            else if (modifier == Modifier::SHARED)
                ss << "SHARED ";
            else if (modifier == Modifier::SYNC)
                ss << "SYNC ";
            else if (modifier == Modifier::MACRO)
                ss << "MACRO ";
        }
        if (!type_.empty()) {
            ss << ",";
        }
    }
    if (!type_.empty()) {
        ss << "type: " << type_;
    }
    return ss.str();
}

const string FuncDecl::toString() const {
    stringstream ss;
    ss << "FuncDecl ";
    if (!implMark_.empty()) {
        ss << "implMark: " << implMark_;
    }
    if (!modifiers_.empty()) {
        if (!implMark_.empty()) {
            ss << ", ";
            ss << "modifiers: ";
            for (auto modifier : modifiers_) {
                switch (modifier) {
                case Modifier::ATOMIC:
                    ss << "ATOMIC ";
                    break;
                case Modifier::SHARED:
                    ss << "SHARED ";
                    break;
                case Modifier::SYNC:
                    ss << "SYNC ";
                    break;
                case Modifier::MACRO:
                    ss << "MACRO ";
                    break;
                default:
                    ss << "Unknow";
                    break;
                }
            }
        }
    }
    bool hasPrev = (!modifiers_.empty() || !implMark_.empty());
    if (hasPrev && !name_.empty()) {
        ss << ", ";
    }
    if (!name_.empty()) {
        ss << "name: " << name_;
        hasPrev = true;
    }
    if (hasPrev && !type_.empty()) {
        ss << ", ";
    }
    if (!type_.empty()) {
        ss << "type: " << type_;
    }
    return ss.str();
}

const string LetDecl::toString() const {
    stringstream ss;
    ss << "LetDecl ";
    if (!modifier_.empty()) {
        ss << "modifier: " << modifier_;
    }
    return ss.str();
}

const string UseDecl::toString() const {
    stringstream ss;
    ss << "UseDecl ";
    if (!identDef_.empty()) {
        ss << "identDef: " << identDef_;
        if (!identRef_.empty()) {
            ss << ",";
        }
    }
    if (!identRef_.empty()) {
        ss << "identRef: " << identRef_;
    }
    return ss.str();
}

const string RetStmt::toString() const {
    stringstream ss;
    ss << "RetStmt ";
    if (!modifier_.empty()) {
        ss << "modifier: " << modifier_;
    }
    return ss.str();
}

const string TypeDecl::toString() const {
    stringstream ss;
    ss << "TypeDecl ";

    auto appendWithComma = [&ss](const string &prefix, const string &value) {
        if (!value.empty()) {
            if (!prefix.empty())
                ss << ", ";
            ss << prefix << value;
            return true;
        }
        return false;
    };

    bool hasPrev = appendWithComma("implMark: ", implMark_);
    hasPrev = appendWithComma("ident: ", ident_) || hasPrev;
    appendWithComma("type: ", type_);

    return ss.str();
}

const string EnumDecl::toString() const {
    stringstream ss;
    ss << "EnumDecl ";
    if (!name_.empty()) {
        ss << "name: " << name_;
        if (!type_.empty()) {
            ss << ",";
        }
    }
    if (!type_.empty()) {
        ss << "type: " << type_;
    }
    return ss.str();
}

const string KeyTypePair::toString() const {
    stringstream ss;
    ss << "KeyTypePair ";
    if (!ident_.empty()) {
        ss << "ident: " << ident_;
        if (!type_.empty()) {
            ss << ",";
        }
    }
    if (!type_.empty()) {
        ss << "type: " << type_;
    }
    return ss.str();
}

const string KeyValuePair::toString() const {
    stringstream ss;
    ss << "KeyValuePair ";
    if (!ident_.empty()) {
        ss << "ident: " << ident_;
    }
    return ss.str();
}

const string KeyParamPair::toString() const {
    stringstream ss;
    ss << "KeyParamPair ";

    auto appendWithComma = [&ss](const string &prefix, const string &value) {
        if (!value.empty()) {
            if (!prefix.empty())
                ss << ", ";
            ss << prefix << value;
            return true;
        }
        return false;
    };

    if (isVar_) {
        ss << "Var: ";
    }

    bool hasPrev = appendWithComma("ident: ", ident_);
    appendWithComma("type: ", type_);

    return ss.str();
}

const string IdentList::toString() const {
    stringstream ss;
    ss << "IdentList ";
    if (!idents_.empty()) {
        ss << "idents: ";
        for (auto ident : idents_) {
            ss << ident << " ";
        }
    }
    return ss.str();
}
const string WaitExpr::toString() const {
    stringstream ss;
    ss << "WaitExpr ";
    if (isWait_) {
        ss << "Wait";
    }
    return ss.str();
}
const string CatchClause::toString() const {
    stringstream ss;
    ss << "CatchClause ";
    if (!ident_.empty()) {
        ss << "ident: " << ident_;
        if (!type_.empty()) {
            ss << ",";
        }
    }
    if (!type_.empty()) {
        ss << "type: " << type_;
    }
    return ss.str();
}

const string AssignExpr::toString() const {
    stringstream ss;
    ss << "AssignExpr ";
    if (op_ != AssignOp::INVALID) {
        if (op_ == AssignOp::ASSIGN) {
            ss << "=";
        } else if (op_ == AssignOp::ADD_ASSIGN) {
            ss << "+=";
        } else if (op_ == AssignOp::MINUS_ASSIGN) {
            ss << "-=";
        } else if (op_ == AssignOp::MUL_ASSIGN) {
            ss << "*=";
        } else if (op_ == AssignOp::DIV_ASSIGN) {
            ss << "/=";
        } else if (op_ == AssignOp::MOD_ASSIGN) {
            ss << "%=";
        } else if (op_ == AssignOp::POW_ASSIGN) {
            ss << "^=";
        } else if (op_ == AssignOp::MATRIX_MUL_ASSIGN) {
            ss << "@=";
        } else if (op_ == AssignOp::AND_ASSIGN) {
            ss << "&=";
        } else if (op_ == AssignOp::OR_ASSIGN) {
            ss << "|=";
        }
    }
    return ss.str();
}
const string EqualityExpr::toString() const {
    stringstream ss;
    ss << "EqualityExpr ";
    if (op_ != EqualityOp::INVALID) {
        if (op_ == EqualityOp::STRICT_EQUAL) {
            ss << "===";
        } else if (op_ == EqualityOp::NOT_STRICT_EQUAL) {
            ss << "!==";
        } else if (op_ == EqualityOp::EQUAL) {
            ss << "==";
        } else if (op_ == EqualityOp::NOT_EQUAL) {
            ss << "!=";
        }
    }
    return ss.str();
}

const string RelationalExpr::toString() const {
    stringstream ss;
    ss << "RelationalExpr ";
    if (op_ != RelationalOp::INVALID) {
        if (op_ == RelationalOp::LESS) {
            ss << "<";
        } else if (op_ == RelationalOp::LESS_EQUAL) {
            ss << "<=";
        } else if (op_ == RelationalOp::GREATER) {
            ss << ">";
        } else if (op_ == RelationalOp::GREATER_EQUAL) {
            ss << ">=";
        }
    }
    return ss.str();
}

const string AdditiveExpr::toString() const {
    stringstream ss;
    ss << "AdditiveExpr ";
    if (op_ != AdditiveOp::INVALID) {
        if (op_ == AdditiveOp::ADD) {
            ss << "+";
        } else if (op_ == AdditiveOp::MINUS) {
            ss << "-";
        }
    }
    return ss.str();
}

const string MultiplicativeExpr::toString() const {
    stringstream ss;
    ss << "MultiplicativeExpr ";
    if (op_ != MultiplicativeOp::INVALID) {
        if (op_ == MultiplicativeOp::MUL) {
            ss << "*";
        } else if (op_ == MultiplicativeOp::DIV) {
            ss << "/";
        } else if (op_ == MultiplicativeOp::POW) {
            ss << "^";
        } else if (op_ == MultiplicativeOp::MOD) {
            ss << "%";
        } else if (op_ == MultiplicativeOp::MATRIX_MUL) {
            ss << "@";
        }
    }
    return ss.str();
}

const string NullableExpr::toString() const {
    stringstream ss;
    ss << "NullableExpr ";
    if (op_ != NullableOp::INVALID) {
        if (op_ == NullableOp::QUESTION_QUESTION) {
            ss << "??";
        } else if (op_ == NullableOp::NOT_NOT) {
            ss << "!!";
        }
    }
    return ss.str();
}

const string UnaryExpr::toString() const {
    stringstream ss;
    ss << "UnaryExpr ";
    if (unaryOp_ != UnaryOp::INVALID) {
        if (unaryOp_ == UnaryOp::NOT) {
            ss << "!";
        } else if (unaryOp_ == UnaryOp::LINK) {
            ss << "-";
        } else if (unaryOp_ == UnaryOp::BIT_NOT) {
            ss << "~";
        }
    }
    if (typeOp_ != TypeOp::INVALID) {
        if (typeOp_ == TypeOp::AS) {
            ss << "as";
        } else if (typeOp_ == TypeOp::IS) {
            ss << "is";
        }
    }
    return ss.str();
}

const string LinkExpr::toString() const {
    stringstream ss;
    ss << "LinkExpr ";
    if (op_ != LinkOp::INVALID) {
        if (op_ == LinkOp::ARROW) {
            ss << "->";
        } else if (op_ == LinkOp::QUESTION_ARROW) {
            ss << "?->";
        }
    }
    return ss.str();
}

const string BindExpr::toString() const {
    stringstream ss;
    ss << "BindExpr ";
    if (op_ != BindOp::INVALID) {
        if (op_ == BindOp::DOUBLE_DOT) {
            ss << "::";
        } else if (op_ == BindOp::QUESTION_DOUBLE_DOT) {
            ss << "?::";
        }
    }
    return ss.str();
}

const string WithExpr::toString() const {
    stringstream ss;
    ss << "WithExpr ";
    if (op_ != WithOp::INVALID) {
        if (op_ == WithOp::DOT) {
            ss << ".";
        } else if (op_ == WithOp::QUESTION_DOT) {
            ss << "?.";
        }
    }
    return ss.str();
}

const string AnnoExpr::toString() const {
    stringstream ss;
    ss << "AnnoExpr ";
    if (isAssert_) {
        ss << "assert";
    }
    return ss.str();
}

const string Literal::toString() const {
    stringstream ss;
    ss << "Literal: " << pointerToHex(data_.get()) << ", ";
    const auto &type = data_->type();
    if (type) {
        ss << type->toString();
    } else {
        ss << "NULL";
    }
    ss << ", " << data_->toString();
    return ss.str();
}

const string UnionUnit::toString() const {
    stringstream ss;
    ss << "UnionUnit ";
    if (!ident_.empty()) {
        ss << "ident: " << ident_;
    }
    return ss.str();
}

const string ListType_::toString() const {
    stringstream ss;
    ss << "ListType ";
    ss << "dimension: " << dimension_;
    return ss.str();
}

const string PrimaryType_::toString() const {
    stringstream ss;
    ss << "PrimaryType ";
    if (!type_.empty()) {
        ss << "type: " << type_;
        if (!ident_.empty()) {
            ss << ",";
        }
    }
    if (!ident_.empty()) {
        ss << "ident: " << ident_;
    }
    return ss.str();
}

const string DictExprType::toString() const {
    stringstream ss;
    ss << "DictExprType ";
    if (op_ != DictExprTypeOp::INVALID) {
        if (op_ == DictExprTypeOp::AMPERSAND) {
            ss << "&";
        } else if (op_ == DictExprTypeOp::CARET) {
            ss << "^";
        }
    }
    return ss.str();
}

const string LambdaType::toString() const {
    stringstream ss;
    ss << "LambdaType ";
    if (!modifiers_.empty()) {
        for (auto modifier : modifiers_) {
            if (modifier == LambdaTypeModifiers::ATOMIC) {
                ss << "ATOMIC ";
            } else if (modifier == LambdaTypeModifiers::SHARED) {
                ss << "SHARED ";
            } else if (modifier == LambdaTypeModifiers::SYNC) {
                ss << "SYNC ";
            } else if (modifier == LambdaTypeModifiers::MACRO) {
                ss << "MACRO ";
            }
        }
    }
    return ss.str();
}

const string IdentDef::toString() const {
    stringstream ss;
    ss << "IdentDef ";
    if (!ident_.empty()) {
        ss << "ident: " << ident_;
    }
    return ss.str();
}

const string IdentRef::toString() const {
    stringstream ss;
    ss << "IdentRef ";
    if (!belongsTo_.empty()) {
        ss << "belongsTo: ";
        for(auto belong : belongsTo_) {
            ss << belong << " "; 
        }
    }
    if (!ident_.empty()) {
        if(!belongsTo_.empty())
            ss << ",";
        ss << "ident: " << ident_;
    } 
    return ss.str();
}