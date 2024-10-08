/**
 * Copyright (c) 2024 Beijing Jiaotong University
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
 * Created: Mar. 26, 2024
 * Updated: Oct. 08, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "antlr/OpenCMLLexer.h"

#include "ast.h"
#include "common/function.h"
#include "common/token.h"
#include "utils/log.h"

#define DEBUG_LEVEL -1

using namespace std;
using namespace ast;

template <typename LoadType, typename... Args> node_ptr_t createNode(Args &&...args) {
    return std::make_shared<Node>(std::make_shared<LoadType>(std::forward<Args>(args)...));
}

template <typename DataType, typename... Args> node_ptr_t createDataNode(Args &&...args) {
    return createNode<DataLoad>(std::make_shared<DataType>(std::forward<Args>(args)...));
}

namespace AbstractSyntaxTree::InnerFuncDRefNodes {
node_ptr_t __copy__ = nullptr;
node_ptr_t __cast__ = nullptr;
node_ptr_t __type__ = nullptr;
node_ptr_t __index__ = nullptr;

node_ptr_t __as__ = nullptr;
node_ptr_t __is__ = nullptr;

node_ptr_t __add__ = nullptr;
node_ptr_t __sub__ = nullptr;
node_ptr_t __mul__ = nullptr;
node_ptr_t __div__ = nullptr;
node_ptr_t __mod__ = nullptr;
node_ptr_t __pow__ = nullptr;
node_ptr_t __inter__ = nullptr;
node_ptr_t __union__ = nullptr;

node_ptr_t __assn__ = nullptr;
node_ptr_t __assn_add__ = nullptr;
node_ptr_t __assn_sub__ = nullptr;
node_ptr_t __assn_mul__ = nullptr;
node_ptr_t __assn_div__ = nullptr;
node_ptr_t __assn_mod__ = nullptr;
node_ptr_t __assn_pow__ = nullptr;
node_ptr_t __assn_inter__ = nullptr;
node_ptr_t __assn_union__ = nullptr;

node_ptr_t __lt__ = nullptr;
node_ptr_t __gt__ = nullptr;
node_ptr_t __le__ = nullptr;
node_ptr_t __ge__ = nullptr;
node_ptr_t __eq__ = nullptr;
node_ptr_t __ne__ = nullptr;
node_ptr_t __and__ = nullptr;
node_ptr_t __or__ = nullptr;

node_ptr_t __not__ = nullptr;
node_ptr_t __neg__ = nullptr;
node_ptr_t __rev__ = nullptr;

node_ptr_t __ifexpr__ = nullptr;

unordered_map<string, node_ptr_t> nodesMap;
unordered_map<string, node_ptr_t> opNodesMap;

void init() {
    if (__copy__)
        return;

    __copy__ = createNode<DRefLoad>("__copy__");
    nodesMap["__copy__"] = __copy__;
    __cast__ = createNode<DRefLoad>("__cast__");
    nodesMap["__cast__"] = __cast__;
    __type__ = createNode<DRefLoad>("__type__");
    nodesMap["__type__"] = __type__;
    __index__ = createNode<DRefLoad>("__index__");
    nodesMap["__index__"] = __index__;

    __as__ = createNode<DRefLoad>("__as__");
    nodesMap["__as__"] = __as__;
    opNodesMap["as"] = __as__;
    __is__ = createNode<DRefLoad>("__is__");
    nodesMap["__is__"] = __is__;
    opNodesMap["is"] = __is__;

    __add__ = createNode<DRefLoad>("__add__");
    nodesMap["__add__"] = __add__;
    opNodesMap["+"] = __add__;
    __sub__ = createNode<DRefLoad>("__sub__");
    nodesMap["__sub__"] = __sub__;
    opNodesMap["-"] = __sub__;
    __mul__ = createNode<DRefLoad>("__mul__");
    nodesMap["__mul__"] = __mul__;
    opNodesMap["*"] = __mul__;
    __div__ = createNode<DRefLoad>("__div__");
    nodesMap["__div__"] = __div__;
    opNodesMap["/"] = __div__;
    __mod__ = createNode<DRefLoad>("__mod__");
    nodesMap["__mod__"] = __mod__;
    opNodesMap["%"] = __mod__;
    __pow__ = createNode<DRefLoad>("__pow__");
    nodesMap["__pow__"] = __pow__;
    opNodesMap["^"] = __pow__;
    __inter__ = createNode<DRefLoad>("__inter__");
    nodesMap["__inter__"] = __inter__;
    opNodesMap["&"] = __inter__;
    __union__ = createNode<DRefLoad>("__union__");
    nodesMap["__union__"] = __union__;
    opNodesMap["|"] = __union__;

    __assn__ = createNode<DRefLoad>("__assn__");
    nodesMap["__assn__"] = __assn__;
    opNodesMap["="] = __assn__;
    __assn_add__ = createNode<DRefLoad>("__assn_add__");
    nodesMap["__assn_add__"] = __assn_add__;
    opNodesMap["+="] = __assn_add__;
    __assn_sub__ = createNode<DRefLoad>("__assn_sub__");
    nodesMap["__assn_sub__"] = __assn_sub__;
    opNodesMap["-="] = __assn_sub__;
    __assn_mul__ = createNode<DRefLoad>("__assn_mul__");
    nodesMap["__assn_mul__"] = __assn_mul__;
    opNodesMap["*="] = __assn_mul__;
    __assn_div__ = createNode<DRefLoad>("__assn_div__");
    nodesMap["__assn_div__"] = __assn_div__;
    opNodesMap["/="] = __assn_div__;
    __assn_mod__ = createNode<DRefLoad>("__assn_mod__");
    nodesMap["__assn_mod__"] = __assn_mod__;
    opNodesMap["%="] = __assn_mod__;
    __assn_pow__ = createNode<DRefLoad>("__assn_pow__");
    nodesMap["__assn_pow__"] = __assn_pow__;
    opNodesMap["^="] = __assn_pow__;
    __assn_inter__ = createNode<DRefLoad>("__assn_inter__");
    nodesMap["__assn_inter__"] = __assn_inter__;
    opNodesMap["&="] = __assn_inter__;
    __assn_union__ = createNode<DRefLoad>("__assn_union__");
    nodesMap["__assn_union__"] = __assn_union__;
    opNodesMap["|="] = __assn_union__;

    __lt__ = createNode<DRefLoad>("__lt__");
    nodesMap["__lt__"] = __lt__;
    opNodesMap["<"] = __lt__;
    __gt__ = createNode<DRefLoad>("__gt__");
    nodesMap["__gt__"] = __gt__;
    opNodesMap[">"] = __gt__;
    __le__ = createNode<DRefLoad>("__le__");
    nodesMap["__le__"] = __le__;
    opNodesMap["<="] = __le__;
    __ge__ = createNode<DRefLoad>("__ge__");
    nodesMap["__ge__"] = __ge__;
    opNodesMap[">="] = __ge__;
    __eq__ = createNode<DRefLoad>("__eq__");
    nodesMap["__eq__"] = __eq__;
    opNodesMap["=="] = __eq__;
    __ne__ = createNode<DRefLoad>("__ne__");
    nodesMap["__ne__"] = __ne__;
    opNodesMap["!="] = __ne__;
    __and__ = createNode<DRefLoad>("__and__");
    nodesMap["__and__"] = __and__;
    opNodesMap["&&"] = __and__;
    __or__ = createNode<DRefLoad>("__or__");
    nodesMap["__or__"] = __or__;
    opNodesMap["||"] = __or__;

    __not__ = createNode<DRefLoad>("__not__");
    nodesMap["__not__"] = __not__;
    opNodesMap["!"] = __not__;
    __neg__ = createNode<DRefLoad>("__neg__");
    nodesMap["__neg__"] = __neg__;
    opNodesMap["-"] = __neg__;
    __rev__ = createNode<DRefLoad>("__rev__");
    nodesMap["__rev__"] = __rev__;
    opNodesMap["~"] = __rev__;
}

} // namespace AbstractSyntaxTree::InnerFuncDRefNodes

inline node_ptr_t reparent(node_ptr_t &node, node_ptr_t &parent) {
    *parent << node;
    return parent;
}

inline node_ptr_t linkFunc(node_ptr_t &argsNode, node_ptr_t &funcNode) {
    node_ptr_t linkNode = createNode<LinkLoad>();
    *linkNode << argsNode << funcNode;
    return linkNode;
}

data_ptr_t Constructor::extractStaticData(const node_ptr_t &node) {
    if (node->type() == NodeType::DATA) {
        const auto dataNode = dynamic_pointer_cast<DataLoad>(node->load());
        return dataNode->data();
    } else if (node->type() == NodeType::DREF) {
        const auto refNode = dynamic_pointer_cast<DRefLoad>(node->load());
        return make_shared<RefData>(refNode->ident());
    } else {
        return nullptr;
    }
}

pair<node_ptr_t, data_ptr_t> Constructor::makeRefData(const node_ptr_t &expr) {
    const string indent = to_string(indentIndex_++);
    node_ptr_t refNode = createNode<NRefLoad>(indent);
    *refNode << expr;
    data_ptr_t data = make_shared<RefData>(indent);
    return make_pair(refNode, data);
}

pair<data_ptr_t, bool> Constructor::extractData(const node_ptr_t &node, node_ptr_t &execNode) {
    const data_ptr_t data = extractStaticData(node);
    if (data) {
        return make_pair(data, false);
    } else {
        auto [refNode, refData] = makeRefData(node);
        *execNode << refNode;
        return make_pair(refData, true);
    }
}

pair<data_ptr_t, bool> Constructor::extractData(const node_ptr_t &node, node_ptr_t &execNode, bool &dangling) {
    auto [refData, dang] = extractData(node, execNode);
    if (dang) {
        dangling = true;
    }
    return make_pair(refData, dang);
}

/*
program : stmtList? EOF;
*/
any Constructor::visitProgram(OpenCMLParser::ProgramContext *context) {
    debug(0) << "visitProgram" << endl;
    if (context->stmtList()) {
        root_ = any_cast<node_ptr_t>(visitStmtList(context->stmtList()));
    } else {
        root_ = createNode<ExecLoad>();
    }
    return root_;
};

/*
stmtList : stmt (SEP stmt)* SEP? ;
*/
any Constructor::visitStmtList(OpenCMLParser::StmtListContext *context) {
    debug(0) << "visitStmtList" << endl;
    pushScope();
    node_ptr_t execNode = createNode<ExecLoad>();
    for (const auto &stmt : context->stmt()) {
        *execNode << any_cast<node_ptr_t>(visitStmt(stmt));
    }
    popScope();
    return execNode;
};

/*
stmt
    : letStmt
    | useStmt
    | typeStmt
    | exprStmt
    | waitStmt
    | funcDef
    | retStmt
    ;
*/
any Constructor::visitStmt(OpenCMLParser::StmtContext *context) {
    debug(0) << "visitStmt" << endl;
    switch (context->getAltNumber()) {
    case 1:
        return visitLetStmt(context->letStmt());
        break;
    case 2:
        return visitUseStmt(context->useStmt());
        break;
    case 3:
        return visitTypeStmt(context->typeStmt());
        break;
    case 4:
        return visitExprStmt(context->exprStmt());
        break;
    case 5:
        return visitWaitStmt(context->waitStmt());
        break;
    case 6:
        return visitFuncDef(context->funcDef());
        break;
    case 7:
        return visitRetStmt(context->retStmt());
        break;

    default:
        throw runtime_error("Unknown statement type");
    }
};

/*
letStmt    : (LET | VAR) carrier (':' typeExpr)? '=' entityExpr ;
*/
any Constructor::visitLetStmt(OpenCMLParser::LetStmtContext *context) {
    debug(0) << "visitLetStmt" << endl;
    const auto &[carrierType, carrier] = any_cast<pair<size_t, any>>(visitCarrier(context->carrier()));
    const auto &typeExpr = context->typeExpr();
    type_ptr_t type = nullptr;
    if (typeExpr) {
        type = any_cast<type_ptr_t>(visitTypeExpr(typeExpr));
    }

    bool dangling = false;
    node_ptr_t exprNode = any_cast<node_ptr_t>(visitEntityExpr(context->entityExpr()));
    node_ptr_t execNode = createNode<ExecLoad>();
    auto [exprValue, _] = extractData(exprNode, execNode, dangling);

    node_ptr_t resultNode = nullptr;

    switch (carrierType) {
    case 1: // identRef
    {
        const string &ident = any_cast<string>(carrier);
        node_ptr_t nRefNode = createNode<NRefLoad>(ident);
        node_ptr_t baseNode = nullptr;

        if (type) {
            node_ptr_t dataNode = createDataNode<TupleData>(data_list_t{exprValue, make_shared<NullData>(type)});

            if (dangling) {
                dataNode = reparent(dataNode, execNode);
            }

            baseNode = linkFunc(dataNode, InnerFuncDRefNodes::__cast__);
        } else {
            baseNode = exprNode;
        }

        *nRefNode << baseNode;
        resultNode = nRefNode;
    } break;

    case 2: // bracedIdents
    {
        if (type) {
            const auto &token = context->getStart();
            throw BuildException("Type cannot be specified for multiple identifiers", token);
        }

        const vector<string> &idents = any_cast<vector<string>>(carrier);
        if (idents.size() == 0) {
            const auto &token = context->getStart();
            throw BuildException("Identifier extraction list must not be empty", token);
        }

        for (size_t i = 0; i < idents.size(); i++) {
            const string &ident = idents[i];
            node_ptr_t nRefNode = createNode<NRefLoad>(ident);
            node_ptr_t dataNode =
                createDataNode<TupleData>(data_list_t{exprValue, make_shared<PrimaryData<int32_t>>(i)});
            *nRefNode << linkFunc(dataNode, InnerFuncDRefNodes::__index__);
            *execNode << nRefNode;
        }

        resultNode = execNode;
    } break;

    case 3: // bracketIdents
    {
        if (type) {
            const auto &token = context->getStart();
            throw BuildException("Type cannot be specified for multiple identifiers", token);
        }

        const vector<string> &idents = any_cast<vector<string>>(carrier);
        if (idents.size() == 0) {
            const auto &token = context->getStart();
            throw BuildException("Identifier extraction list must not be empty", token);
        }

        for (size_t i = 0; i < idents.size(); i++) {
            const string &ident = idents[i];
            node_ptr_t nRefNode = createNode<NRefLoad>(ident);
            node_ptr_t dataNode = createDataNode<TupleData>(data_list_t{exprValue, make_shared<StringData>(ident)});
            *nRefNode << linkFunc(dataNode, InnerFuncDRefNodes::__index__);
            *execNode << nRefNode;
        }

        resultNode = execNode;
    } break;

    default:
        throw runtime_error("Unknown carrier type");
    }

    if (context->VAR()) {
        node_ptr_t variNode = createNode<VariLoad>();
        *variNode << resultNode;
        return variNode;
    }

    return resultNode;
};

/*
useStmt    : USE (identRef | bracedIdents | '*') FROM STRING ;
*/
any Constructor::visitUseStmt(OpenCMLParser::UseStmtContext *context) {
    debug(0) << "visitUseStmt" << endl;
    string path = context->STRING()->getText();
    vector<string> idents;

    if (context->identRef()) {
        idents.push_back(any_cast<string>(visitIdentRef(context->identRef())));
    } else if (context->bracedIdents()) {
        idents = any_cast<vector<string>>(visitBracedIdents(context->bracedIdents()));
    }

    return createNode<FromLoad>(path, idents);
};

/*
typeStmt : TYPE identRef '=' typeExpr ;
*/
any Constructor::visitTypeStmt(OpenCMLParser::TypeStmtContext *context) {
    debug(0) << "visitTypeStmt" << endl;
    const string &ident = context->identRef()->getText();
    type_ptr_t type = any_cast<type_ptr_t>(visitTypeExpr(context->typeExpr()));
    typeScope_->insert(ident, type);
    return createNode<TypeLoad>(type);
};

/*
exprStmt : annotations? entityExpr ;
*/
any Constructor::visitExprStmt(OpenCMLParser::ExprStmtContext *context) {
    debug(0) << "visitExprStmt" << endl;
    return visitEntityExpr(context->entityExpr());
};

/*
waitStmt   : WAIT entityExpr ;
*/
any Constructor::visitWaitStmt(OpenCMLParser::WaitStmtContext *context) {
    debug(0) << "visitExprStmt" << endl;
    return visitEntityExpr(context->entityExpr());
};

/*
withDef : WITH angledParams ;
*/
any Constructor::visitWithDef(OpenCMLParser::WithDefContext *context) {
    debug(0) << "visitWithDef" << endl;
    return visitAngledParams(context->angledParams());
};

/*
funcDef    : annotations? withDef? modifiers? FUNC identRef parentParams (':' typeExpr)? bracedStmts ;
*/
any Constructor::visitFuncDef(OpenCMLParser::FuncDefContext *context) {
    debug(0) << "visitFuncDef" << endl;
    // TODO: Implement annotations
    const string ident = any_cast<string>(visitIdentRef(context->identRef()));
    shared_ptr<FunctorType> funcType = nullptr;
    const auto withType = make_shared<ParamsType>();
    const auto paramsType = make_shared<ParamsType>();

    const auto &typeExpr = context->typeExpr();
    if (typeExpr) {
        const auto returnType = any_cast<type_ptr_t>(visitTypeExpr(typeExpr));
        funcType = make_shared<FunctorType>(withType, paramsType, returnType);
    } else {
        // if no return type is specified, the default return type is void
        funcType = make_shared<FunctorType>(withType, paramsType, voidTypePtr);
    }

    const auto &withDef = context->withDef();
    if (withDef) {
        const auto &pairedParams = any_cast<vector<tuple<string, type_ptr_t, data_ptr_t, bool>>>(visitWithDef(withDef));
        for (const auto &[name, type, data, isVar] : pairedParams) {
            withType->add(name, type, data);
            bool success = funcType->addIdent(name, isVar);
            if (!success) {
                const auto &token = context->getStart();
                throw BuildException("Identifier '" + name + "' already exists in the function signature", token);
            }
        }
    }

    const auto &params =
        any_cast<vector<tuple<string, type_ptr_t, data_ptr_t, bool>>>(visitParentParams(context->parentParams()));
    for (const auto &[name, type, data, isVar] : params) {
        paramsType->add(name, type, data);
        bool success = funcType->addIdent(name, isVar);
        if (!success) {
            const auto &token = context->getStart();
            throw BuildException("Identifier '" + name + "' already exists in the function signature", token);
        }
    }

    const auto &modifiers = context->modifiers();
    if (modifiers) {
        const auto &modSet = any_cast<unordered_set<FunctorModifier>>(visitModifiers(modifiers));
        funcType->setModifiers(modSet);
        try {
            funcType->checkModifiers();
        } catch (const exception &e) {
            const auto &token = modifiers->getStart();
            throw BuildException(e.what(), token);
        }
    }

    const auto funcTypeNode = createNode<TypeLoad>(dynamic_pointer_cast<Type>(funcType));
    const auto funcNode = createNode<FuncLoad>();
    *funcNode << funcTypeNode << any_cast<node_ptr_t>(visitBracedStmts(context->bracedStmts()));

    node_ptr_t nRefNode = createNode<NRefLoad>(ident);
    *nRefNode << funcNode;

    return nRefNode;
};

/*
retStmt : RETURN entityExpr? ;
*/
any Constructor::visitRetStmt(OpenCMLParser::RetStmtContext *context) {
    debug(0) << "visitRetStmt" << endl;
    node_ptr_t retNode = createNode<RetnLoad>();
    if (context->entityExpr()) {
        *retNode << any_cast<node_ptr_t>(visitEntityExpr(context->entityExpr()));
    }
    return retNode;
};

/*
lambdaExpr : modifiers? angledParams? parentParams (':' typeExpr)? '=>' (bracedStmts | entityExpr) ;
*/
any Constructor::visitLambdaExpr(OpenCMLParser::LambdaExprContext *context) {
    debug(0) << "visitLambdaExpr" << endl;
    shared_ptr<FunctorType> funcType = nullptr;
    node_ptr_t bodyNode = nullptr;
    const auto withType = make_shared<ParamsType>();
    const auto paramsType = make_shared<ParamsType>();

    const auto &typeExpr = context->typeExpr();
    if (typeExpr) {
        const auto returnType = any_cast<type_ptr_t>(visitTypeExpr(typeExpr));
        funcType = make_shared<FunctorType>(withType, paramsType, returnType);
    } else {
        // if no return type is specified, the default return type is void
        funcType = make_shared<FunctorType>(withType, paramsType, voidTypePtr);
    }

    if (context->angledParams()) {
        const auto &withParams =
            any_cast<vector<tuple<string, type_ptr_t, data_ptr_t, bool>>>(visitAngledParams(context->angledParams()));
        for (const auto &[name, type, data, isVar] : withParams) {
            withType->add(name, type, data);
            bool success = funcType->addIdent(name, isVar);
            if (!success) {
                const auto &token = context->getStart();
                throw BuildException("Identifier '" + name + "' already exists in the function signature", token);
            }
        }
    }

    const auto &params =
        any_cast<vector<tuple<string, type_ptr_t, data_ptr_t, bool>>>(visitParentParams(context->parentParams()));
    for (const auto &[name, type, data, isVar] : params) {
        paramsType->add(name, type, data);
        bool success = funcType->addIdent(name, isVar);
        if (!success) {
            const auto &token = context->getStart();
            throw BuildException("Identifier '" + name + "' already exists in the function signature", token);
        }
    }

    const auto &stmts = context->bracedStmts();
    if (stmts) {
        bodyNode = any_cast<node_ptr_t>(visitBracedStmts(stmts));
    } else {
        node_ptr_t exprNode = any_cast<node_ptr_t>(visitEntityExpr(context->entityExpr()));
        node_ptr_t returnNode = createNode<RetnLoad>();
        node_ptr_t execNode = createNode<ExecLoad>();
        *returnNode << exprNode;
        *execNode << returnNode;
        bodyNode = execNode;
    }

    const auto &modifiers = context->modifiers();
    if (modifiers) {
        const auto &modSet = any_cast<unordered_set<FunctorModifier>>(visitModifiers(modifiers));
        funcType->setModifiers(modSet);
        try {
            funcType->checkModifiers();
        } catch (const exception &e) {
            const auto &token = modifiers->getStart();
            throw BuildException(e.what(), token);
        }
    }

    const auto funcTypeNode = createNode<TypeLoad>(funcType);
    const auto funcNode = createNode<FuncLoad>();

    *funcNode << funcTypeNode << bodyNode;
    return funcNode;
};

/*
carrier    : identRef | bracedIdents | bracketIdents ;
*/
any Constructor::visitCarrier(OpenCMLParser::CarrierContext *context) {
    debug(0) << "visitCarrier" << endl;
    const size_t alt = context->getAltNumber();
    switch (alt) {
    case 1:
        return make_pair(alt, visitIdentRef(context->identRef()));
        break;
    case 2:
        return make_pair(alt, visitBracedIdents(context->bracedIdents()));
        break;
    case 3:
        return make_pair(alt, visitBracketIdents(context->bracketIdents()));
        break;

    default:
        throw runtime_error("Unknown carrier type");
    }
};

/*
annotation  : '@' primaryExpr ;
*/
any Constructor::visitAnnotation(OpenCMLParser::AnnotationContext *context) {
    debug(0) << "visitAnnotation" << endl;
    return visitPrimaryExpr(context->primaryExpr());
};

/*
annotations : annotation+ ;
*/
any Constructor::visitAnnotations(OpenCMLParser::AnnotationsContext *context) {
    debug(0) << "visitAnnotations" << endl;
    // TODO: Implement visitAnnotations
    node_ptr_t execNode = createNode<ExecLoad>();
    for (const auto &annotation : context->annotation()) {
        *execNode << any_cast<node_ptr_t>(visitAnnotation(annotation));
    }
    return execNode;
};

/*
modifiers   : (INNER | OUTER | ATOMIC | SHARED)+ ;
*/
any Constructor::visitModifiers(OpenCMLParser::ModifiersContext *context) {
    debug(0) << "visitModifiers" << endl;
    unordered_set<FunctorModifier> modifiers;
    for (const auto &mod : context->children) {
        modifiers.insert(str2modifier(mod->getText()));
    }
    return modifiers;
};

/*
keyTypePair  : identRef ':' typeExpr ;
*/
any Constructor::visitKeyTypePair(OpenCMLParser::KeyTypePairContext *context) {
    debug(0) << "visitKeyTypePair" << endl;
    string key = any_cast<string>(visitIdentRef(context->identRef()));
    return make_pair(key, any_cast<type_ptr_t>(visitTypeExpr(context->typeExpr())));
};

/*
keyValuePair : identRef ':' entityExpr ;
*/
any Constructor::visitKeyValuePair(OpenCMLParser::KeyValuePairContext *context) {
    debug(0) << "visitKeyValuePair" << endl;
    return make_pair(context->identRef()->getText(), any_cast<node_ptr_t>(visitEntityExpr(context->entityExpr())));
};

/*
keyParamPair : VAR? identRef annotation? ':' typeExpr ('=' entityExpr)? ;
*/
any Constructor::visitKeyParamPair(OpenCMLParser::KeyParamPairContext *context) {
    debug(0) << "visitKeyParamPair" << endl;
    bool isVar = context->VAR() != nullptr;
    type_ptr_t type = any_cast<type_ptr_t>(visitTypeExpr(context->typeExpr()));
    data_ptr_t defaultData = nullptr;
    if (context->entityExpr()) {
        const auto defaultNode = any_cast<node_ptr_t>(visitEntityExpr(context->entityExpr()));
        defaultData = extractStaticData(defaultNode);
        if (!defaultData) {
            const auto &exprToken = context->entityExpr()->getStart();
            // TODO shouldn't throw exception here
            throw BuildException("Default data must not be expressions", exprToken);
        }
    }
    return make_tuple(context->identRef()->getText(), type, defaultData, isVar);
};

/*
indexKTPair  : '[' typeExpr ']' ':' typeExpr ;
*/
any Constructor::visitIndexKTPair(OpenCMLParser::IndexKTPairContext *context) {
    debug(0) << "visitIndexKTPair" << endl;
    const auto &keyType = any_cast<type_ptr_t>(visitTypeExpr(context->typeExpr(0)));
    const auto &valType = any_cast<type_ptr_t>(visitTypeExpr(context->typeExpr(1)));
    return make_pair(keyType, valType);
};

/*
indexKVPair  : '[' entityExpr ']' ':' entityExpr ;
*/
any Constructor::visitIndexKVPair(OpenCMLParser::IndexKVPairContext *context) {
    debug(0) << "visitIndexKVPair" << endl;
    const auto &key = any_cast<node_ptr_t>(visitEntityExpr(context->entityExpr(0)));
    const auto &data = any_cast<node_ptr_t>(visitEntityExpr(context->entityExpr(1)));
    return make_pair(key, data);
};

/*
typeList     : typeExpr (',' typeExpr)* ;
*/
any Constructor::visitTypeList(OpenCMLParser::TypeListContext *context) {
    debug(0) << "visitTypeList" << endl;
    vector<type_ptr_t> typeList;
    for (const auto &type : context->typeExpr()) {
        typeList.push_back(any_cast<type_ptr_t>(visitTypeExpr(type)));
    }
    return typeList;
};

/*
identList    : identRef (',' identRef)* ;
*/
any Constructor::visitIdentList(OpenCMLParser::IdentListContext *context) {
    debug(0) << "visitIdentList" << endl;
    vector<string> identList;
    for (const auto &ident : context->identRef()) {
        identList.push_back(ident->getText());
    }
    return identList;
};

/*
valueList    : entityExpr (',' entityExpr)* ;
*/
any Constructor::visitValueList(OpenCMLParser::ValueListContext *context) {
    debug(0) << "visitValueList" << endl;
    vector<node_ptr_t> valueList;
    for (const auto &data : context->entityExpr()) {
        valueList.push_back(any_cast<node_ptr_t>(visitEntityExpr(data)));
    }
    return valueList;
};

/*
pairedTypes  : keyTypePair (',' keyTypePair)* ;
*/
any Constructor::visitPairedTypes(OpenCMLParser::PairedTypesContext *context) {
    debug(0) << "visitPairedTypes" << endl;
    vector<pair<string, type_ptr_t>> pairedTypes;
    for (const auto &kvPair : context->keyTypePair()) {
        pairedTypes.push_back(any_cast<pair<string, type_ptr_t>>(visitKeyTypePair(kvPair)));
    }
    return pairedTypes;
};

/*
pairedValues : keyValuePair (',' keyValuePair)* ;
*/
any Constructor::visitPairedValues(OpenCMLParser::PairedValuesContext *context) {
    debug(0) << "visitPairedValues" << endl;
    map<string, node_ptr_t> dataMap;
    for (const auto &kvPair : context->keyValuePair()) {
        dataMap.insert(any_cast<pair<string, node_ptr_t>>(visitKeyValuePair(kvPair)));
    }
    return dataMap;
};

/*
pairedParams : keyParamPair (',' keyParamPair)* ;
*/
any Constructor::visitPairedParams(OpenCMLParser::PairedParamsContext *context) {
    debug(0) << "visitPairedParams" << endl;
    vector<tuple<string, type_ptr_t, data_ptr_t, bool>> pairedParams;
    for (const auto &pair : context->keyParamPair()) {
        pairedParams.push_back(any_cast<tuple<string, type_ptr_t, data_ptr_t, bool>>(visitKeyParamPair(pair)));
    }
    return pairedParams;
};

/*
indexKVPairs : indexKVPair (',' indexKVPair)* ;
*/
any Constructor::visitIndexKVPairs(OpenCMLParser::IndexKVPairsContext *context) {
    debug(0) << "visitIndexKVPairs" << endl;
    vector<pair<node_ptr_t, node_ptr_t>> indexKVPairs;
    for (const auto &kvPair : context->indexKVPair()) {
        indexKVPairs.push_back(any_cast<pair<node_ptr_t, node_ptr_t>>(visitIndexKVPair(kvPair)));
    }
    return indexKVPairs;
};

/*
argumentList : valueList (',' pairedValues)? | pairedValues ;
*/
any Constructor::visitArgumentList(OpenCMLParser::ArgumentListContext *context) {
    debug(0) << "visitArgumentList" << endl;
    const auto &valueList = context->valueList();
    const auto &pairedValues = context->pairedValues();
    vector<node_ptr_t> indexArgs;
    if (valueList) {
        indexArgs = any_cast<vector<node_ptr_t>>(visitValueList(valueList));
    }
    map<string, node_ptr_t> namedArgs;
    if (pairedValues) {
        namedArgs = any_cast<map<string, node_ptr_t>>(visitPairedValues(pairedValues));
    }
    return make_pair(indexArgs, namedArgs);
};

/*
bracedPairedValues : '{' pairedValues? ','? '}' ;
*/
any Constructor::visitBracedPairedValues(OpenCMLParser::BracedPairedValuesContext *context) {
    debug(0) << "visitBracedPairedValues" << endl;
    const auto &pairedValues = context->pairedValues();
    if (pairedValues) {
        return visitPairedValues(pairedValues);
    } else {
        return map<string, node_ptr_t>();
    }
};

/*
bracedIdents       : '{' identList? ','? '}' ;
*/
any Constructor::visitBracedIdents(OpenCMLParser::BracedIdentsContext *context) {
    debug(0) << "visitBracedIdents" << endl;
    const auto &identList = context->identList();
    if (identList) {
        return visitIdentList(identList);
    } else {
        return vector<string>();
    }
};

/*
bracedStmts        : '{' stmtList? '}' ;
*/
any Constructor::visitBracedStmts(OpenCMLParser::BracedStmtsContext *context) {
    debug(0) << "visitBracedStmts" << endl;
    if (context->stmtList()) {
        return visitStmtList(context->stmtList());
    } else {
        return createNode<ExecLoad>();
    }
};

/*
bracedValues       : '{' valueList? ','? '}' ;
*/
any Constructor::visitBracedValues(OpenCMLParser::BracedValuesContext *context) {
    debug(0) << "visitBracedValues" << endl;
    const auto &valueList = context->valueList();
    if (valueList) {
        return visitValueList(valueList);
    } else {
        return vector<node_ptr_t>();
    }
};

/*
bracedIndexKVPairs : '{' indexKVPairs? ','? '}' ;
*/
any Constructor::visitBracedIndexKVPairs(OpenCMLParser::BracedIndexKVPairsContext *context) {
    debug(0) << "visitBracedIndexKVPairs" << endl;
    const auto &indexKVPairs = context->indexKVPairs();
    if (indexKVPairs) {
        return visitIndexKVPairs(indexKVPairs);
    } else {
        return vector<pair<node_ptr_t, node_ptr_t>>();
    }
};

/*
bracketIdents : '[' identList? ','? ']' ;
*/
any Constructor::visitBracketIdents(OpenCMLParser::BracketIdentsContext *context) {
    debug(0) << "visitBracketIdents" << endl;
    const auto &identList = context->identList();
    if (identList) {
        return visitIdentList(identList);
    } else {
        return vector<string>();
    }
};

/*
bracketValues : '[' valueList? ','? ']' ;
*/
any Constructor::visitBracketValues(OpenCMLParser::BracketValuesContext *context) {
    debug(0) << "visitBracketValues" << endl;
    const auto &valueList = context->valueList();
    if (valueList) {
        return visitValueList(valueList);
    } else {
        return vector<node_ptr_t>();
    }
};

/*
memberAccess : '[' entityExpr ']' ;
*/
any Constructor::visitMemberAccess(OpenCMLParser::MemberAccessContext *context) {
    debug(0) << "visitMemberAccess" << endl;
    return visitEntityExpr(context->entityExpr());
};

/*
parentParams : '(' pairedParams? ','? ')' ;
*/
any Constructor::visitParentParams(OpenCMLParser::ParentParamsContext *context) {
    debug(0) << "visitParentParams" << endl;
    const auto &pairedParams = context->pairedParams();
    if (pairedParams) {
        return visitPairedParams(pairedParams);
    } else {
        return vector<tuple<string, type_ptr_t, data_ptr_t, bool>>();
    }
};

/*
parentArgues       : '(' argumentList? ','? ')' ;
*/
any Constructor::visitParentArgues(OpenCMLParser::ParentArguesContext *context) {
    debug(0) << "visitParentArgues" << endl;
    const auto &argumentList = context->argumentList();
    if (argumentList) {
        return visitArgumentList(argumentList);
    } else {
        return make_pair(vector<node_ptr_t>(), map<string, node_ptr_t>());
    }
};

/*
parentValues       : '(' valueList? ','? ')' ;
*/
any Constructor::visitParentValues(OpenCMLParser::ParentValuesContext *context) {
    debug(0) << "visitParentValues" << endl;
    const auto &valueList = context->valueList();
    if (valueList) {
        return visitValueList(valueList);
    } else {
        return vector<node_ptr_t>();
    }
};

/*
angledParams       : '<' pairedParams? ','? '>' ;
*/
any Constructor::visitAngledParams(OpenCMLParser::AngledParamsContext *context) {
    debug(0) << "visitAngledParams" << endl;
    const auto &pairedParams = context->pairedParams();
    if (pairedParams) {
        return visitPairedParams(pairedParams);
    } else {
        return vector<tuple<string, type_ptr_t, data_ptr_t, bool>>();
    }
};

/*
angledValues       : '<' argumentList? ','? '>' ;
*/
any Constructor::visitAngledValues(OpenCMLParser::AngledValuesContext *context) {
    debug(0) << "visitAngledValues" << endl;
    const auto &argumentList = context->argumentList();
    if (argumentList) {
        return visitArgumentList(argumentList);
    } else {
        return make_pair(vector<node_ptr_t>(), map<string, node_ptr_t>());
    }
};

/*
entityExpr
    : ternaryExpr (('=' | '+=' | '-=' | '*=' | '/=' | '%=' | '^=' | '&=' | '|=') ternaryExpr)?
    ;
*/
any Constructor::visitEntityExpr(OpenCMLParser::EntityExprContext *context) {
    debug(0) << "visitEntityExpr" << endl;
    return visitBinaryOpList(context, context->ternaryExpr());
};

/*
ternaryExpr
    : relationalExpr ('?' ternaryExpr ':' ternaryExpr)?
    ;
*/
any Constructor::visitTernaryExpr(OpenCMLParser::TernaryExprContext *context) {
    debug(0) << "visitTernaryExpr" << endl;
    const auto &relationalExpr = context->relationalExpr();
    node_ptr_t condNode = any_cast<node_ptr_t>(visitRelationalExpr(relationalExpr));

    if (context->children.size() > 1) {
        node_ptr_t execNode = createNode<ExecLoad>();
        node_ptr_t trueNode = any_cast<node_ptr_t>(visitTernaryExpr(context->ternaryExpr(0)));
        node_ptr_t falseNode = any_cast<node_ptr_t>(visitTernaryExpr(context->ternaryExpr(1)));

        auto [condData, condDang] = extractData(condNode, execNode);
        auto [trueData, trueDang] = extractData(trueNode, execNode);
        auto [falseData, falseDang] = extractData(falseNode, execNode);

        node_ptr_t dataNode = createDataNode<TupleData>(data_list_t{condData, trueData, falseData});

        if (condDang || trueDang || falseDang) {
            dataNode = reparent(dataNode, execNode);
        }

        return linkFunc(dataNode, InnerFuncDRefNodes::__ifexpr__);
    }

    return condNode;
};

/*
logicalOrExpr
    : logicalAndExpr ('||' logicalAndExpr)*
    ;
*/
any Constructor::visitLogicalOrExpr(OpenCMLParser::LogicalOrExprContext *context) {
    debug(0) << "visitLogicalOrExpr" << endl;
    return visitBinaryOpList(context, context->logicalAndExpr());
};

/*
logicalAndExpr
    : equalityExpr ('&&' equalityExpr)*
    ;
*/
any Constructor::visitLogicalAndExpr(OpenCMLParser::LogicalAndExprContext *context) {
    debug(0) << "visitLogicalAndExpr" << endl;
    return visitBinaryOpList(context, context->equalityExpr());
};

/*
equalityExpr
    : relationalExpr (('==' | '!=') relationalExpr)*
    ;
*/
any Constructor::visitEqualityExpr(OpenCMLParser::EqualityExprContext *context) {
    debug(0) << "visitEqualityExpr" << endl;
    return visitBinaryOpList(context, context->relationalExpr());
};

/*
relationalExpr
    : additiveExpr (('<' | '>' | '<=' | '>=') additiveExpr)*
    ;
*/
any Constructor::visitRelationalExpr(OpenCMLParser::RelationalExprContext *context) {
    debug(0) << "visitRelationalExpr" << endl;
    return visitBinaryOpList(context, context->additiveExpr());
};

/*
additiveExpr
    : multiplicativeExpr (('+' | '-') multiplicativeExpr)*
    ;
*/
any Constructor::visitAdditiveExpr(OpenCMLParser::AdditiveExprContext *context) {
    debug(0) << "visitAdditiveExpr" << endl;
    return visitBinaryOpList(context, context->multiplicativeExpr());
};

/*
multiplicativeExpr
    : unaryExpr (('^' | '*' | '/' | '%' | AS | IS) unaryExpr)*
    ;
*/
any Constructor::visitMultiplicativeExpr(OpenCMLParser::MultiplicativeExprContext *context) {
    debug(0) << "visitMultiplicativeExpr" << endl;
    return visitBinaryOpList(context, context->unaryExpr());
};

/*
unaryExpr
    : linkExpr
    | ('!' | '-' | '~') linkExpr
    ;
*/
any Constructor::visitUnaryExpr(OpenCMLParser::UnaryExprContext *context) {
    debug(0) << "visitUnaryExpr" << endl;
    if (context->linkExpr()) {
        return visitLinkExpr(context->linkExpr());
    }

    node_ptr_t execNode = createNode<ExecLoad>();
    node_ptr_t linkNode = any_cast<node_ptr_t>(visitLinkExpr(context->linkExpr()));

    string op = context->children[0]->getText();
    node_ptr_t funcNode = InnerFuncDRefNodes::opNodesMap[op];

    auto [linkData, linkDang] = extractData(linkNode, execNode);
    node_ptr_t dataNode = createDataNode<TupleData>(data_list_t{linkData});

    if (linkDang) {
        dataNode = reparent(dataNode, execNode);
    }

    return linkFunc(dataNode, funcNode);
};

/*
linkExpr
    : withExpr ('->' withExpr)*
    ;
*/
any Constructor::visitLinkExpr(OpenCMLParser::LinkExprContext *context) {
    debug(0) << "visitLinkExpr" << endl;
    const auto &withExprs = context->withExpr();
    node_ptr_t lhsNode = any_cast<node_ptr_t>(visitWithExpr(withExprs[0]));

    for (size_t i = 1; i < withExprs.size(); ++i) {
        node_ptr_t execNode = createNode<ExecLoad>();
        node_ptr_t rhsNode = any_cast<node_ptr_t>(visitWithExpr(withExprs[i]));

        node_ptr_t linkNode = createNode<LinkLoad>();
        *linkNode << lhsNode << rhsNode;

        lhsNode = linkNode;
    }

    return lhsNode;
};

/*
withExpr
    : annotatedExpr ('.' annotatedExpr)*
    ;
*/
any Constructor::visitWithExpr(OpenCMLParser::WithExprContext *context) {
    debug(0) << "visitWithExpr" << endl;
    const auto &annotatedExprs = context->annotatedExpr();
    node_ptr_t lhsNode = any_cast<node_ptr_t>(visitAnnotatedExpr(annotatedExprs[0]));

    for (size_t i = 1; i < annotatedExprs.size(); ++i) {
        node_ptr_t execNode = createNode<ExecLoad>();
        node_ptr_t rhsNode = any_cast<node_ptr_t>(visitAnnotatedExpr(annotatedExprs[i]));

        node_ptr_t linkNode = createNode<LinkLoad>();
        *linkNode << lhsNode << rhsNode;

        lhsNode = linkNode;
    }

    return lhsNode;
};

/*
annotatedExpr
    : primaryExpr (memberAccess | parentArgues | angledValues | annotation)*
    ;
*/
any Constructor::visitAnnotatedExpr(OpenCMLParser::AnnotatedExprContext *context) {
    debug(0) << "visitAnnotatedExpr" << endl;
    node_ptr_t lhsNode = any_cast<node_ptr_t>(visitPrimaryExpr(context->primaryExpr()));

    for (size_t i = 1; i < context->children.size(); ++i) {
        auto child = context->children[i];
        const char &op = child->children[0]->getText()[0];
        switch (op) {
        case '[': {
            node_ptr_t rhsNode = any_cast<node_ptr_t>(visit(child));
            node_ptr_t execNode = createNode<ExecLoad>();
            auto [resultData, resultDang] = extractData(lhsNode, execNode);
            auto [memberData, memberDang] = extractData(rhsNode, execNode);
            node_ptr_t dataNode = createDataNode<TupleData>(data_list_t{resultData, memberData});
            if (resultDang || memberDang) {
                dataNode = reparent(dataNode, execNode);
            }
            lhsNode = linkFunc(dataNode, InnerFuncDRefNodes::__index__);
        } break;

        case '(': {
            auto [rawIndexArgs, rawNamedArgs] =
                any_cast<pair<vector<node_ptr_t>, map<string, node_ptr_t>>>(visit(child));
            auto &paramsPtr = make_shared<ParamsData>();
            node_ptr_t execNode = createNode<ExecLoad>();
            bool dangling = false;
            for (const auto &arg : rawIndexArgs) {
                auto [argData, _] = extractData(arg, execNode, dangling);
                paramsPtr->emplace(argData);
            }
            for (const auto &[name, arg] : rawNamedArgs) {
                auto [argData, _] = extractData(arg, execNode, dangling);
                paramsPtr->emplace(argData, name);
            }
            node_ptr_t dataNode = createNode<DataLoad>(paramsPtr);
            node_ptr_t linkNode = createNode<LinkLoad>();
            *linkNode << dataNode << lhsNode;
            if (dangling) {
                lhsNode = reparent(linkNode, execNode);
            } else {
                lhsNode = linkNode;
            }
        } break;

        case '<': {
            auto [rawIndexArgs, rawNamedArgs] =
                any_cast<pair<vector<node_ptr_t>, map<string, node_ptr_t>>>(visit(child));
            auto &paramsPtr = make_shared<ParamsData>();
            node_ptr_t execNode = createNode<ExecLoad>();
            bool dangling = false;
            for (const auto &arg : rawIndexArgs) {
                auto [argData, _] = extractData(arg, execNode, dangling);
                paramsPtr->emplace(argData);
            }
            for (const auto &[name, arg] : rawNamedArgs) {
                auto [argData, _] = extractData(arg, execNode, dangling);
                paramsPtr->emplace(argData, name);
            }
            node_ptr_t dataNode = createNode<DataLoad>(paramsPtr);
            node_ptr_t withNode = createNode<WithLoad>();
            *withNode << dataNode << lhsNode;
            if (dangling) {
                lhsNode = reparent(withNode, execNode);
            } else {
                lhsNode = withNode;
            }
        } break;

        case '@': {
            // TODO: Implement annotation
        } break;
        }
    }

    return lhsNode;
};

/*
primaryExpr
    : identRef
    | literal
    | bracketValues         // for list
    | bracedPairedValues    // for dict
    | '(' entityExpr ')'    // if there is only one entity, it will be recognized as a primary expression rather than a
tuple | parentValues          // for tuple
    \\ for vector | array | tensor | set | map
    | '<' typeExpr (',' (typeExpr | INTEGER | '[' INTEGER (',' INTEGER)* ']'))? '>' (bracketValues | bracedValues |
bracedIndexKVPairs) | lambdaExpr ;
*/
any Constructor::visitPrimaryExpr(OpenCMLParser::PrimaryExprContext *context) {
    debug(0) << "visitPrimaryExpr: " << context->getAltNumber() << endl;
    switch (context->getAltNumber()) {
    case 1: { // identRef
        const string &ident = any_cast<string>(visitIdentRef(context->identRef()));
        return createNode<DRefLoad>(ident);
    } break;
    case 2: { // literal
        return visitLiteral(context->literal());
        break;
    }
    case 3: { // bracketValues (for list)
        const vector<node_ptr_t> &dataVec = any_cast<vector<node_ptr_t>>(visitBracketValues(context->bracketValues()));
        const auto &listData = make_shared<ListData>();
        bool dangling = false;
        node_ptr_t execNode = createNode<ExecLoad>();
        for (const auto &node : dataVec) {
            auto [data, _] = extractData(node, execNode, dangling);
            listData->emplace(data);
        }
        node_ptr_t dataNode = createNode<DataLoad>(listData);
        if (dangling) {
            return reparent(dataNode, execNode);
        } else {
            return dataNode;
        }
    } break;
    case 4: { // bracedPairedValues (for dict)
        const map<string, node_ptr_t> &dataVec =
            any_cast<map<string, node_ptr_t>>(visitBracedPairedValues(context->bracedPairedValues()));
        const auto &dictData = make_shared<DictData>();
        bool dangling = false;
        node_ptr_t execNode = createNode<ExecLoad>();
        for (const auto &[key, node] : dataVec) {
            auto [data, _] = extractData(node, execNode, dangling);
            dictData->emplace(key, data);
        }
        node_ptr_t dataNode = createNode<DataLoad>(dictData);
        if (dangling) {
            return reparent(dataNode, execNode);
        } else {
            return dataNode;
        }
    } break;
    case 5: { // '(' entityExpr ')'
        return visitEntityExpr(context->entityExpr());
    } break;
    case 6: { // parentValues (for tuple)
        const vector<node_ptr_t> &dataVec = any_cast<vector<node_ptr_t>>(visitParentValues(context->parentValues()));
        auto &tuplePtr = make_shared<TupleData>();
        bool dangling = false;
        node_ptr_t execNode = createNode<ExecLoad>();
        for (const auto &node : dataVec) {
            auto [data, _] = extractData(node, execNode, dangling);
            tuplePtr->emplace(data);
        }
        node_ptr_t dataNode = createNode<DataLoad>(tuplePtr);
        if (dangling) {
            dataNode = reparent(dataNode, execNode);
        }
        return dataNode;
    } break;
    // TODO: Need type checking for the following cases
    case 7: { // '<' typeExpr (',' (typeExpr | INTEGER | '[' INTEGER (',' INTEGER)* ']'))? '>'
              // (bracketValues | bracedValues | bracedIndexKVPairs)
              // for vector | array | tensor | set | map
        const auto &typeExprs = context->typeExpr();
        const auto &integers = context->INTEGER();
        if (context->bracketValues()) { // Vector <T> [] | Array <T, N> [] | Tensor <T, [N1, N2]> []
            if (typeExprs.size() == 1) {
                // typeExprs.size() == 1: Vector <T> [] | Array <T, N> [] | Tensor <T, [N1, N2]> []
                if (integers.size() == 0) { // Vector <T> []
                    const type_ptr_t &type = any_cast<type_ptr_t>(visitTypeExpr(typeExprs[0]));
                    const vector<node_ptr_t> &dataVec =
                        any_cast<vector<node_ptr_t>>(visitBracketValues(context->bracketValues()));
                    const auto &vectorPtr = make_shared<VectorData>(type);
                    bool dangling = false;
                    node_ptr_t execNode = createNode<ExecLoad>();
                    for (const auto &node : dataVec) {
                        auto [data, _] = extractData(node, execNode, dangling);
                        vectorPtr->emplace(data);
                    }
                    node_ptr_t dataNode = createNode<DataLoad>(vectorPtr);
                    if (dangling) {
                        dataNode = reparent(dataNode, execNode);
                    }
                    return dataNode;
                } else if (integers.size() == 1) { // Array <T, N> []
                    const type_ptr_t &type = any_cast<type_ptr_t>(visitTypeExpr(typeExprs[0]));
                    const int size = stoi(integers[0]->getText());
                    const vector<node_ptr_t> &dataVec =
                        any_cast<vector<node_ptr_t>>(visitBracketValues(context->bracketValues()));
                    const auto &arrayPtr = make_shared<ArrayData>(type, size);
                    bool dangling = false;
                    node_ptr_t execNode = createNode<ExecLoad>();
                    for (size_t i = 0; i < dataVec.size(); ++i) {
                        auto [data, _] = extractData(dataVec[i], execNode, dangling);
                        arrayPtr->emplace(data, i);
                    }
                    node_ptr_t dataNode = createNode<DataLoad>(arrayPtr);
                    if (dangling) {
                        dataNode = reparent(dataNode, execNode);
                    }
                    return dataNode;
                } else { // Tensor <T, [N1, N2]> []
                    const type_ptr_t &type = any_cast<type_ptr_t>(visitTypeExpr(typeExprs[0]));
                    vector<size_t> shape;
                    for (size_t i = 0; i < integers.size(); ++i) {
                        shape.push_back(stoi(integers[i]->getText()));
                    }
                    const vector<node_ptr_t> &dataVec =
                        any_cast<vector<node_ptr_t>>(visitBracketValues(context->bracketValues()));
                    const auto &tensorPtr = make_shared<TensorData>(type, shape);
                    bool dangling = false;
                    node_ptr_t execNode = createNode<ExecLoad>();
                    // TODO: Implement tensor data setting
                    reportWarning("Tensor data setting is not implemented yet", context->getStart());
                    node_ptr_t dataNode = createNode<DataLoad>(tensorPtr);
                    if (dangling) {
                        dataNode = reparent(dataNode, execNode);
                    }
                    return dataNode;
                }
            } else {
                reportWarning("Multiple type specification is not supported for list-like literal data",
                              context->getStart());
            }
        } else if (context->bracedValues()) { // Set <T> {} | Map <T1, T2> {} (must be empty)
            if (typeExprs.size() == 1) {      // Set <T> {}
                const type_ptr_t &type = any_cast<type_ptr_t>(visitTypeExpr(typeExprs[0]));
                const vector<node_ptr_t> &dataVec =
                    any_cast<vector<node_ptr_t>>(visitBracedValues(context->bracedValues()));
                const auto &setPtr = make_shared<SetData>(type);
                bool dangling = false;
                node_ptr_t execNode = createNode<ExecLoad>();
                for (const auto &node : dataVec) {
                    auto [data, _] = extractData(node, execNode, dangling);
                    setPtr->emplace(data);
                }
                node_ptr_t dataNode = createNode<DataLoad>(setPtr);
                if (dangling) {
                    dataNode = reparent(dataNode, execNode);
                }
                return dataNode;
            } else { // Map <T1, T2> {}
                const type_ptr_t &type1 = any_cast<type_ptr_t>(visitTypeExpr(typeExprs[0]));
                const type_ptr_t &type2 = any_cast<type_ptr_t>(visitTypeExpr(typeExprs[1]));
                const vector<node_ptr_t> &dataVec =
                    any_cast<vector<node_ptr_t>>(visitBracedValues(context->bracedValues()));
                if (dataVec.size() > 0) {
                    throw BuildException("Map literal dataVec must be in the form of { [K]: V }", context->getStart());
                }
                const auto &mapData = make_shared<MapData>(type1, type2);
                return createNode<DataLoad>(mapData);
            }
        } else if (context->bracedIndexKVPairs()) { // Map <T1, T2> { [K]: V }
            if (typeExprs.size() == 1) {
                throw BuildException("Map literal must have two type specifications", context->getStart());
            }
            const type_ptr_t &type1 = any_cast<type_ptr_t>(visitTypeExpr(typeExprs[0]));
            const type_ptr_t &type2 = any_cast<type_ptr_t>(visitTypeExpr(typeExprs[1]));
            const vector<pair<node_ptr_t, node_ptr_t>> &dataVec =
                any_cast<vector<pair<node_ptr_t, node_ptr_t>>>(visitBracedIndexKVPairs(context->bracedIndexKVPairs()));
            auto &mapPtr = make_shared<MapData>(type1, type2);
            bool dangling = false;
            node_ptr_t execNode = createNode<ExecLoad>();
            for (const auto &[key, data] : dataVec) {
                auto [keyData, _] = extractData(key, execNode, dangling);
                auto [valData, _] = extractData(data, execNode, dangling);
                mapPtr->emplace(keyData, valData);
            }
            node_ptr_t dataNode = createNode<DataLoad>(mapPtr);
            if (dangling) {
                dataNode = reparent(dataNode, execNode);
            }
            return dataNode;
        }
        throw BuildException("Invalid type specification, no pattern matched", context->getStart());
    }
    case 8: { // lambdaExpr
        return visitLambdaExpr(context->lambdaExpr());
    } break;
    }
};

/*
literal
    : INTEGER UNIT?
    | REAL UNIT?
    | STRING
    | MULTI_STR
    | FSTRING
    | TRUE
    | FALSE
    | NULL
    ;
*/
any Constructor::visitLiteral(OpenCMLParser::LiteralContext *context) {
    debug(0) << "visitLiteral: " << context->getAltNumber() << endl;
    data_ptr_t data = nullptr;

    switch (context->getAltNumber()) {
    case 1: // INTEGER UNIT?
        data = dynamic_pointer_cast<Data>(
            make_shared<PrimaryData<int64_t>>(parseNumber<int64_t>(context->INTEGER()->getText())));
        break;
    case 2: // REAL UNIT?
        data = dynamic_pointer_cast<Data>(
            make_shared<PrimaryData<double>>(parseNumber<double>(context->REAL()->getText())));
        break;
    case 3: // STRING
    {
        const auto &text = context->STRING()->getText();
        data = dynamic_pointer_cast<Data>(make_shared<StringData>(text.substr(1, text.size() - 2)));
    } break;
    case 4: // MULTI_STR
    {
        const auto &text = context->MULTI_STR()->getText();
        data = dynamic_pointer_cast<Data>(make_shared<StringData>(text.substr(3, text.size() - 6)));
    } break;
    case 5: // FSTRING
    {
        // TODO: Implement FSTRING
        const auto &text = context->FSTRING()->getText();
        data = dynamic_pointer_cast<Data>(make_shared<StringData>(text.substr(2, text.size() - 3)));
    } break;
    case 6: // TRUE
        data = dynamic_pointer_cast<Data>(make_shared<PrimaryData<bool>>(true));
        break;
    case 7: // FALSE
        data = dynamic_pointer_cast<Data>(make_shared<PrimaryData<bool>>(false));
        break;
    case 8: // NULL
        data = dynamic_pointer_cast<Data>(make_shared<NullData>());
        break;

    default:
        break;
    }

    return createNode<DataLoad>(data);
};

/*
typeExpr
    : arrayType (('&' | '|' | '^') arrayType)*
    ;
*/
any Constructor::visitTypeExpr(OpenCMLParser::TypeExprContext *context) {
    debug(0) << "visitTypeExpr" << endl;
    type_ptr_t lhsType = any_cast<type_ptr_t>(visitArrayType(context->arrayType(0)));
    for (size_t i = 1; i < context->arrayType().size(); ++i) {
        const string &op = context->children[i * 2 - 1]->getText();
        type_ptr_t rhsType = any_cast<type_ptr_t>(visitArrayType(context->arrayType(i)));
        if (op == "&") {
            if (lhsType->code() != TypeCode::DICT) {
                const auto &token = context->getStart();
                throw BuildException("The left-hand side of '&' must be a dict type", token);
            }
            if (rhsType->code() != TypeCode::DICT) {
                const auto &token = context->getStart();
                throw BuildException("The right-hand side of '&' must be a dict type", token);
            }
            lhsType = dynamic_pointer_cast<Type>(dynamic_cast<DictType &>(*lhsType.get()) &
                                                 dynamic_cast<DictType &>(*rhsType.get()));
        } else if (op == "|") {
            if (lhsType->code() == TypeCode::DICT && rhsType->code() == TypeCode::DICT) {
                lhsType = dynamic_pointer_cast<Type>(dynamic_cast<DictType &>(*lhsType.get()) |
                                                     dynamic_cast<DictType &>(*rhsType.get()));
            } else {
                lhsType = dynamic_pointer_cast<Type>(make_shared<UnionType>(lhsType, rhsType));
            }
        } else if (op == "^") {
            // TODO: Implement '^' operator
            reportWarning("The '^' operator is not implemented yet", context->getStart());
            return lhsType;
        }
    }
    return lhsType;
};

/*
arrayType
    : atomType ('[' INTEGER? ']')*
    ;
*/
any Constructor::visitArrayType(OpenCMLParser::ArrayTypeContext *context) {
    debug(0) << "visitArrayType" << endl;
    type_ptr_t type = any_cast<type_ptr_t>(visitAtomType(context->atomType()));
    for (size_t i = 1; i < context->children.size(); i++) {
        if (context->children[i]->getText() == "[") {
            const auto &size = context->children[i + 1]->getText();
            if (size == "]") {
                type = dynamic_pointer_cast<Type>(make_shared<VectorType>(type));
            } else {
                type = dynamic_pointer_cast<Type>(make_shared<ArrayType>(type, stoi(size)));
            }
        }
    }
    return type;
};

/*
atomType
    : primaryType
    | structType
    | specialType
    | identRef
    | '(' typeExpr ')'
    | lambdaType
    ;
*/
any Constructor::visitAtomType(OpenCMLParser::AtomTypeContext *context) {
    debug(0) << "visitAtomType: " << context->getAltNumber() << endl;
    switch (context->getAltNumber()) {
    case 1: // primType
        return visitPrimaryType(context->primaryType());
        break;

    case 2: // structType
        return visitStructType(context->structType());
        break;

    case 3: // specialType
        return visitSpecialType(context->specialType());
        break;

    case 4: // identRef
    {
        const auto &ident = any_cast<string>(visitIdentRef(context->identRef()));
        const auto &type = typeScope_->at(ident);
        if (!type.has_value()) {
            const auto &token = context->getStart();
            throw BuildException("Type '" + ident + "' is not defined", token);
        }
        return type.value();
    } break;

    case 5: // '(' typeExpr ')'
        return visitTypeExpr(context->typeExpr());
        break;

    case 6: // lambdaType
        return visitLambdaType(context->lambdaType());
        break;

    default:
        throw runtime_error("Unknown atom type");
    }
};

/*
lambdaType
    : modifiers? angledParams? parentParams '=>' typeExpr
    ;
*/
any Constructor::visitLambdaType(OpenCMLParser::LambdaTypeContext *context) {
    debug(0) << "visitLambdaType" << endl;
    shared_ptr<FunctorType> funcType = nullptr;
    node_ptr_t bodyNode = nullptr;
    const auto withType = make_shared<ParamsType>();
    const auto paramsType = make_shared<ParamsType>();

    const auto &typeExpr = context->typeExpr();
    if (typeExpr) {
        const auto returnType = any_cast<type_ptr_t>(visitTypeExpr(typeExpr));
        funcType = make_shared<FunctorType>(withType, paramsType, returnType);
    } else {
        // if no return type is specified, the default return type is void
        funcType = make_shared<FunctorType>(withType, paramsType, voidTypePtr);
    }

    if (context->angledParams()) {
        const auto &withParams =
            any_cast<vector<tuple<string, type_ptr_t, data_ptr_t, bool>>>(visitAngledParams(context->angledParams()));
        for (const auto &[name, type, data, isVar] : withParams) {
            withType->add(name, type, data);
            bool success = funcType->addIdent(name, isVar);
            if (!success) {
                const auto &token = context->getStart();
                throw BuildException("Identifier '" + name + "' already exists in the function signature", token);
            }
        }
    }

    const auto &params =
        any_cast<vector<tuple<string, type_ptr_t, data_ptr_t, bool>>>(visitParentParams(context->parentParams()));
    for (const auto &[name, type, data, isVar] : params) {
        paramsType->add(name, type, data);
        bool success = funcType->addIdent(name, isVar);
        if (!success) {
            const auto &token = context->getStart();
            throw BuildException("Identifier '" + name + "' already exists in the function signature", token);
        }
    }

    const auto &modifiers = context->modifiers();
    if (modifiers) {
        const auto &modSet = any_cast<unordered_set<FunctorModifier>>(visitModifiers(modifiers));
        funcType->setModifiers(modSet);
        try {
            funcType->checkModifiers();
        } catch (const exception &e) {
            const auto &token = modifiers->getStart();
            throw BuildException(e.what(), token);
        }
    }

    return funcType;
};

/*
primaryType
    : INTEGER_TYPE
    | INTEGER32_TYPE
    | INTEGER64_TYPE
    | REAL_TYPE
    | FLOAT_TYPE
    | DOUBLE_TYPE
    | NUMBER_TYPE
    | STRING_TYPE
    | BOOL_TYPE
    | CHAR_TYPE
    ;
*/
any Constructor::visitPrimaryType(OpenCMLParser::PrimaryTypeContext *context) {
    debug(0) << "visitPrimaryType" << context->getAltNumber() << endl;
    const auto tokenType = context->getStart()->getType();
    switch (tokenType) {
    case OpenCMLLexer::INTEGER_TYPE: // INTEGER_TYPE
        return intTypePtr;
        break;
    case OpenCMLLexer::INTEGER32_TYPE: // INTEGER32_TYPE
        return int32TypePtr;
        break;
    case OpenCMLLexer::INTEGER64_TYPE: // INTEGER64_TYPE
        return int64TypePtr;
        break;
    case OpenCMLLexer::REAL_TYPE: // REAL_TYPE
        return realTypePtr;
        break;
    case OpenCMLLexer::FLOAT_TYPE: // FLOAT_TYPE
        return floatTypePtr;
        break;
    case OpenCMLLexer::DOUBLE_TYPE: // DOUBLE_TYPE
        return doubleTypePtr;
        break;
    case OpenCMLLexer::NUMBER_TYPE: // NUMBER_TYPE
        return numberTypePtr;
        break;
    case OpenCMLLexer::STRING_TYPE: // STRING_TYPE
        return stringTypePtr;
        break;
    case OpenCMLLexer::BOOL_TYPE: // BOOL_TYPE
        return boolTypePtr;
        break;
    case OpenCMLLexer::CHAR_TYPE: // CHAR_TYPE
        return charTypePtr;
        break;

    default:
        throw runtime_error("Unknown primary type");
    }
};

/*
structType
    : SET_TYPE ('<' typeExpr '>')?
    | MAP_TYPE ('<' typeExpr ',' typeExpr '>')?
    | LIST_TYPE // variable length, heterogeneous
    | DICT_TYPE // universal dict type
    | ARRAY_TYPE ('<' typeExpr (',' INTEGER)? '>')? // fixed length, homogenous
    | TUPLE_TYPE ('<' typeList? ','? '>')? // fixed length, heterogeneous
    | UNION_TYPE ('<' typeList? ','? '>')?
    | VECTOR_TYPE ('<' typeExpr '>')? // variable length, homogenous
    | TENSOR_TYPE ('<' typeExpr (',' '[' INTEGER (',' INTEGER)* ']')? '>')?
    | '{' pairedTypes? ','? '}' // concrete dict type
    | '{' indexKTPair '}' // concrete map type
    ;
*/
any Constructor::visitStructType(OpenCMLParser::StructTypeContext *context) {
    debug(0) << "visitStructType: " << context->getAltNumber() << endl;
    switch (context->getAltNumber()) {
    case 1: // SET_TYPE ('<' typeExpr '>')?
    {
        const auto &typeExpr = context->typeExpr();
        if (typeExpr.size() == 1) {
            const auto &type = any_cast<type_ptr_t>(visitTypeExpr(typeExpr[0]));
            const auto &setType = make_shared<SetType>(type);
            return dynamic_pointer_cast<Type>(setType);
        } else {
            // if no type is specified, use any type
            return dynamic_pointer_cast<Type>(make_shared<SetType>(anyTypePtr));
        }
    } break;

    case 2: // MAP_TYPE ('<' typeExpr ',' typeExpr '>')?
    {
        const auto &typeExpr = context->typeExpr();
        if (typeExpr.size() == 2) {
            const auto &keyType = any_cast<type_ptr_t>(visitTypeExpr(typeExpr[0]));
            const auto &valType = any_cast<type_ptr_t>(visitTypeExpr(typeExpr[1]));
            const auto &mapType = make_shared<MapType>(keyType, valType);
            return dynamic_pointer_cast<Type>(mapType);
        } else {
            // if no type is specified, use any type
            return dynamic_pointer_cast<Type>(make_shared<MapType>(anyTypePtr, anyTypePtr));
        }
    } break;

    case 3: // LIST_TYPE
        return listTypePtr;
        break;

    case 4: // DICT_TYPE
        // TODO: Implement the support for universal dict type
        reportWarning("Universal dict type is not supported yet", context->getStart());
        return dynamic_pointer_cast<Type>(make_shared<DictType>());
        break;

    case 5: // ARRAY_TYPE ('<' typeExpr (',' INTEGER)? '>')?
    {
        const auto &typeExpr = context->typeExpr();
        if (typeExpr.size() == 1) {
            const auto &type = any_cast<type_ptr_t>(visitTypeExpr(typeExpr[0]));
            const auto &sizes = context->INTEGER();
            if (sizes.size() == 0) {
                throw BuildException("Array type must have a size", context->getStart());
            }
            const auto &arrayType = make_shared<ArrayType>(type, stoi(sizes[0]->getText()));
            return dynamic_pointer_cast<Type>(arrayType);
        } else {
            throw BuildException("Array type must have a type specification", context->getStart());
        }
    } break;

    case 6: // TUPLE_TYPE ('<' typeList? ','? '>')?
    {
        const auto &typeList = context->typeList();
        if (typeList) {
            const auto &types = any_cast<vector<type_ptr_t>>(visitTypeList(typeList));
            const auto &tupleType = make_shared<TupleType>(types);
            return dynamic_pointer_cast<Type>(tupleType);
        } else {
            return dynamic_pointer_cast<Type>(make_shared<TupleType>());
        }
    } break;

    case 7: // UNION_TYPE ('<' typeList? ','? '>')?
    {
        const auto &typeList = context->typeList();
        if (typeList) {
            const auto &types = any_cast<vector<type_ptr_t>>(visitTypeList(typeList));
            const auto &unionType = make_shared<UnionType>(types);
            return dynamic_pointer_cast<Type>(unionType);
        } else {
            return dynamic_pointer_cast<Type>(make_shared<UnionType>());
        }
    } break;

    case 8: // VECTOR_TYPE ('<' typeExpr '>')?
    {
        const auto &typeExpr = context->typeExpr();
        if (typeExpr.size() == 1) {
            const auto &type = any_cast<type_ptr_t>(visitTypeExpr(typeExpr[0]));
            const auto &vectorType = make_shared<VectorType>(type);
            return dynamic_pointer_cast<Type>(vectorType);
        } else {
            throw BuildException("Vector type must have a type specification", context->getStart());
        }
    } break;

    case 9: // TENSOR_TYPE ('<' typeExpr (',' '[' INTEGER (',' INTEGER)* ']')? '>')?
    {
        const auto &typeExpr = context->typeExpr();
        if (typeExpr.size() == 1) {
            const auto &type = any_cast<type_ptr_t>(visitTypeExpr(typeExpr[0]));
            const auto &sizes = context->INTEGER();
            if (sizes.size()) {
                vector<size_t> dimensions;
                for (const auto &size : sizes) {
                    dimensions.push_back(stoi(size->getText()));
                }
                const auto tensorType = make_shared<TensorType>(type, dimensions);
                return dynamic_pointer_cast<Type>(tensorType);
            } else {
                // if no size is specified, use a scalar tensor
                const auto tensorType = make_shared<TensorType>(type, vector<size_t>());
                return dynamic_pointer_cast<Type>(tensorType);
            }
        } else {
            throw BuildException("Tensor type must have a type specification", context->getStart());
        }
    } break;

    case 10: // '{' pairedTypes? ','? '}'
    {
        const auto &pairedTypes = context->pairedTypes();
        if (pairedTypes) {
            const auto &types = any_cast<vector<pair<string, type_ptr_t>>>(visitPairedTypes(pairedTypes));
            auto dictType = make_shared<DictType>();
            for (const auto &[key, type] : types) {
                dictType->add(key, type);
            }
            return dynamic_pointer_cast<Type>(dictType);
        } else {
            return dynamic_pointer_cast<Type>(make_shared<DictType>());
        }
    } break;

    case 11: // '{' indexKTPair '}'
    {
        const auto &indexKTPair = context->indexKTPair();
        const auto &keyType = any_cast<type_ptr_t>(visitIndexKTPair(indexKTPair));
        return dynamic_pointer_cast<Type>(make_shared<MapType>(keyType, anyTypePtr));
    } break;

    default:
        throw runtime_error("Unknown struct type");
    }
};

/*
specialType
    : ANY_TYPE
    | VOID_TYPE
    | FUNCTOR_TYPE
    ;
*/
any Constructor::visitSpecialType(OpenCMLParser::SpecialTypeContext *context) {
    debug(0) << "visitSpecialType" << endl;
    const auto &tokenType = context->getStart()->getType();
    switch (tokenType) {
    case OpenCMLLexer::ANY_TYPE: // ANY_TYPE
        return anyTypePtr;
        break;
    case OpenCMLLexer::VOID_TYPE: // VOID_TYPE
        return voidTypePtr;
        break;
    case OpenCMLLexer::FUNCTOR_TYPE: // FUNCTOR_TYPE
        return functorTypePtr;
        break;

    default:
        throw runtime_error("Unknown special type");
    }
};

/*
identRef : IDENTIFIER ;
*/
any Constructor::visitIdentRef(OpenCMLParser::IdentRefContext *context) {
    debug(0) << "visitIdentRef" << endl;
    return context->IDENTIFIER()->getText();
};
