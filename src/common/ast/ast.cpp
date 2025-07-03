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
    case LoadType::Program:
        return "Program";
    case LoadType::Decl:
        return "Decl";
    case LoadType::ModuleDecl:
        return "ModuleDecl";
    case LoadType::ImportDecl:
        return "ImportDecl";
    case LoadType::ExportDecl:
        return "ExportDecl";
    case LoadType::LetDecl:
        return "LetDecl";
    case LoadType::UseDecl:
        return "UseDecl";
    case LoadType::FuncDecl:
        return "FuncDecl";
    case LoadType::TypeDecl:
        return "TypeDecl";
    case LoadType::EnumDecl:
        return "EnumDecl";
    case LoadType::Stmt:
        return "Stmt";
    case LoadType::StmtList:
        return "StmtList";
    case LoadType::StmtBlock:
        return "StmtBlock";
    case LoadType::RetStmt:
        return "RetStmt";
    case LoadType::WaitStmt:
        return "WaitStmt";
    case LoadType::LambdaExpr:
        return "LambdaExpr";
    case LoadType::KeyTypePair:
        return "KeyTypePair";
    case LoadType::KeyValuePair:
        return "KeyValuePair";
    case LoadType::KeyParamPair:
        return "KeyParamPair";
    case LoadType::IdentDefList:
        return "IdentDefList";
    case LoadType::ValueList:
        return "ValueList";
    case LoadType::IndexValues:
        return "IndexValues";
    case LoadType::PairedValues:
        return "PairedValues";
    case LoadType::PairedParams:
        return "PairedParams";
    case LoadType::ArgumentList:
        return "ArgumentList";
    case LoadType::MemberAccess:
        return "MemberAccess";
    case LoadType::DataExpr:
        return "DataExpr";
    case LoadType::IdentList:
        return "IdentList";
    case LoadType::DataList:
        return "DataList";
    case LoadType::CtrlExpr:
        return "CtrlExpr";
    case LoadType::IfLoad:
        return "IfLoad";
    case LoadType::CatchClause:
        return "CatchClause";
    case LoadType::BlockExpr:
        return "BlockExpr";
    case LoadType::WaitExpr:
        return "WaitExpr";
    case LoadType::Wildcard:
        return "Wildcard";
    case LoadType::MatchCase:
        return "MatchCase";
    case LoadType::AssignExpr:
        return "AssignExpr";
    case LoadType::LogicalOrExpr:
        return "LogicalOrExpr";
    case LoadType::LogicalAndExpr:
        return "LogicalAndExpr";
    case LoadType::EqualityExpr:
        return "EqualityExpr";
    case LoadType::RelationalExpr:
        return "RelationalExpr";
    case LoadType::AdditiveExpr:
        return "AdditiveExpr";
    case LoadType::MultiplicativeExpr:
        return "MultiplicativeExpr";
    case LoadType::NullableExpr:
        return "NullableExpr";
    case LoadType::UnaryExpr:
        return "UnaryExpr";
    case LoadType::LinkExpr:
        return "LinkExpr";
    case LoadType::BindExpr:
        return "BindExpr";
    case LoadType::WithExpr:
        return "WithExpr";
    case LoadType::AnnoExpr:
        return "AnnoExpr";
    case LoadType::DictExpr:
        return "DictExpr";
    case LoadType::DictType:
        return "DictType";
    case LoadType::ListExpr:
        return "ListExpr";
    case LoadType::Literal:
        return "Literal";
    case LoadType::TypeExpr:
        return "TypeExpr";
    case LoadType::UnionType:
        return "UnionType";
    case LoadType::UnionUnit:
        return "UnionUnit";
    case LoadType::ListType:
        return "ListType";
    case LoadType::ArgsType:
        return "ArgsType";
    case LoadType::PrimaryType:
        return "PrimaryType";
    case LoadType::DictExprType:
        return "DictExprType";
    case LoadType::TypeList:
        return "TypeList";
    case LoadType::LambdaType:
        return "LambdaType";
    case LoadType::IdentDef:
        return "IdentDef";
    case LoadType::IdentRef:
        return "IdentRef";
    case LoadType::ParentArgues:
        return "ParentArgues";
    case LoadType::ParentParams:
        return "ParentParams";
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
            ss << ",";
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
    if (hasPrev && !ident_.empty()) {
        ss << ",";
    }
    if (!ident_.empty()) {
        ss << "ident: " << ident_;
        hasPrev = true;
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
    if (!implMark_.empty()) {
        ss << "implMark: " << implMark_;
        if (!ident_.empty()) {
            ss << ",";
        }
    }
    if(!ident_.empty()) {
        ss << "ident: " << ident_; 
    }
    return ss.str();
}

const string EnumDecl::toString() const {
    stringstream ss;
    ss << "EnumDecl ";
    if (!name_.empty()) {
        ss << "name: " << name_;
    }
    return ss.str();
}

const string KeyTypePair::toString() const {
    stringstream ss;
    ss << "KeyTypePair ";
    if (!ident_.empty()) {
        ss << "ident: " << ident_;
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
    if(isVar_)
    {
        ss << "var";
        if(!ident_.empty()) {
            ss << " ";
        }
    }
    if(!ident_.empty()) {
        ss << "ident: " << ident_;
    }
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

const string ParentArgues::toString() const {
    stringstream ss;
    ss << "ParentArgues ";
    ss << "()";
    return ss.str();
}

const string ParentParams::toString() const {
    stringstream ss;
    ss << "ParentParams ";
    ss << "()"; 
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
    if (!ident_.empty()) {
        ss << "ident: " << ident_;
        if(!type_.empty()) {
            ss << ","; 
        }
    }
    if(!type_.empty()) {
        ss << "type: " << type_;
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

const string FuncType::toString() const {
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