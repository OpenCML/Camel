// /**
//  * Copyright (c) 2024 the OpenCML Organization
//  * Camel is licensed under the MIT license.
//  * You can use this software according to the terms and conditions of the
//  * MIT license. You may obtain a copy of the MIT license at:
//  * [https://opensource.org/license/mit]
//  *
//  * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
//  * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
//  * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//  *
//  * See the the MIT license for more details.
//  *
//  * Author: Zhenjie Wei
//  * Created: Mar. 26, 2024
//  * Updated: Mar. 10, 2025
//  * Supported by: National Key Research and Development Program of China
//  */

// #include "parse/antlr/OpenCMLLexer.h"

// #include "common/token.h"
// #include "gct.h"
// #include "utils/log.h"

// #define DEBUG_LEVEL 0

// using namespace std;
// using namespace GCT;

// namespace GraphConstructTree::InnerFuncDRefNodes {
// node_ptr_t __copy__ = nullptr;
// node_ptr_t __cast__ = nullptr;
// node_ptr_t __type__ = nullptr;
// node_ptr_t __index__ = nullptr;

// node_ptr_t __as__ = nullptr;
// node_ptr_t __is__ = nullptr;

// node_ptr_t __add__ = nullptr;
// node_ptr_t __sub__ = nullptr;
// node_ptr_t __mul__ = nullptr;
// node_ptr_t __div__ = nullptr;
// node_ptr_t __mod__ = nullptr;
// node_ptr_t __pow__ = nullptr;
// node_ptr_t __inter__ = nullptr;
// node_ptr_t __union__ = nullptr;

// node_ptr_t __assn__ = nullptr;
// node_ptr_t __assn_add__ = nullptr;
// node_ptr_t __assn_sub__ = nullptr;
// node_ptr_t __assn_mul__ = nullptr;
// node_ptr_t __assn_div__ = nullptr;
// node_ptr_t __assn_mod__ = nullptr;
// node_ptr_t __assn_pow__ = nullptr;
// node_ptr_t __assn_inter__ = nullptr;
// node_ptr_t __assn_union__ = nullptr;

// node_ptr_t __lt__ = nullptr;
// node_ptr_t __gt__ = nullptr;
// node_ptr_t __le__ = nullptr;
// node_ptr_t __ge__ = nullptr;
// node_ptr_t __eq__ = nullptr;
// node_ptr_t __ne__ = nullptr;
// node_ptr_t __and__ = nullptr;
// node_ptr_t __or__ = nullptr;

// node_ptr_t __not__ = nullptr;
// node_ptr_t __neg__ = nullptr;
// node_ptr_t __rev__ = nullptr;

// node_ptr_t __ifexpr__ = nullptr;

// unordered_map<string, node_ptr_t> nodesMap;
// unordered_map<string, node_ptr_t> opNodesMap;

// void init() {
//     if (__copy__)
//         return;

//     __copy__ = createNodeAs<DRefLoad>("__copy__");
//     nodesMap["__copy__"] = __copy__;
//     __cast__ = createNodeAs<DRefLoad>("__cast__");
//     nodesMap["__cast__"] = __cast__;
//     __type__ = createNodeAs<DRefLoad>("__type__");
//     nodesMap["__type__"] = __type__;
//     __index__ = createNodeAs<DRefLoad>("__index__");
//     nodesMap["__index__"] = __index__;

//     __as__ = createNodeAs<DRefLoad>("__as__");
//     nodesMap["__as__"] = __as__;
//     opNodesMap["as"] = __as__;
//     __is__ = createNodeAs<DRefLoad>("__is__");
//     nodesMap["__is__"] = __is__;
//     opNodesMap["is"] = __is__;

//     __add__ = createNodeAs<DRefLoad>("__add__");
//     nodesMap["__add__"] = __add__;
//     opNodesMap["+"] = __add__;
//     __sub__ = createNodeAs<DRefLoad>("__sub__");
//     nodesMap["__sub__"] = __sub__;
//     opNodesMap["-"] = __sub__;
//     __mul__ = createNodeAs<DRefLoad>("__mul__");
//     nodesMap["__mul__"] = __mul__;
//     opNodesMap["*"] = __mul__;
//     __div__ = createNodeAs<DRefLoad>("__div__");
//     nodesMap["__div__"] = __div__;
//     opNodesMap["/"] = __div__;
//     __mod__ = createNodeAs<DRefLoad>("__mod__");
//     nodesMap["__mod__"] = __mod__;
//     opNodesMap["%"] = __mod__;
//     __pow__ = createNodeAs<DRefLoad>("__pow__");
//     nodesMap["__pow__"] = __pow__;
//     opNodesMap["^"] = __pow__;
//     __inter__ = createNodeAs<DRefLoad>("__inter__");
//     nodesMap["__inter__"] = __inter__;
//     opNodesMap["&"] = __inter__;
//     __union__ = createNodeAs<DRefLoad>("__union__");
//     nodesMap["__union__"] = __union__;
//     opNodesMap["|"] = __union__;

//     __assn__ = createNodeAs<DRefLoad>("__assn__");
//     nodesMap["__assn__"] = __assn__;
//     opNodesMap["="] = __assn__;
//     __assn_add__ = createNodeAs<DRefLoad>("__assn_add__");
//     nodesMap["__assn_add__"] = __assn_add__;
//     opNodesMap["+="] = __assn_add__;
//     __assn_sub__ = createNodeAs<DRefLoad>("__assn_sub__");
//     nodesMap["__assn_sub__"] = __assn_sub__;
//     opNodesMap["-="] = __assn_sub__;
//     __assn_mul__ = createNodeAs<DRefLoad>("__assn_mul__");
//     nodesMap["__assn_mul__"] = __assn_mul__;
//     opNodesMap["*="] = __assn_mul__;
//     __assn_div__ = createNodeAs<DRefLoad>("__assn_div__");
//     nodesMap["__assn_div__"] = __assn_div__;
//     opNodesMap["/="] = __assn_div__;
//     __assn_mod__ = createNodeAs<DRefLoad>("__assn_mod__");
//     nodesMap["__assn_mod__"] = __assn_mod__;
//     opNodesMap["%="] = __assn_mod__;
//     __assn_pow__ = createNodeAs<DRefLoad>("__assn_pow__");
//     nodesMap["__assn_pow__"] = __assn_pow__;
//     opNodesMap["^="] = __assn_pow__;
//     __assn_inter__ = createNodeAs<DRefLoad>("__assn_inter__");
//     nodesMap["__assn_inter__"] = __assn_inter__;
//     opNodesMap["&="] = __assn_inter__;
//     __assn_union__ = createNodeAs<DRefLoad>("__assn_union__");
//     nodesMap["__assn_union__"] = __assn_union__;
//     opNodesMap["|="] = __assn_union__;

//     __lt__ = createNodeAs<DRefLoad>("__lt__");
//     nodesMap["__lt__"] = __lt__;
//     opNodesMap["<"] = __lt__;
//     __gt__ = createNodeAs<DRefLoad>("__gt__");
//     nodesMap["__gt__"] = __gt__;
//     opNodesMap[">"] = __gt__;
//     __le__ = createNodeAs<DRefLoad>("__le__");
//     nodesMap["__le__"] = __le__;
//     opNodesMap["<="] = __le__;
//     __ge__ = createNodeAs<DRefLoad>("__ge__");
//     nodesMap["__ge__"] = __ge__;
//     opNodesMap[">="] = __ge__;
//     __eq__ = createNodeAs<DRefLoad>("__eq__");
//     nodesMap["__eq__"] = __eq__;
//     opNodesMap["=="] = __eq__;
//     __ne__ = createNodeAs<DRefLoad>("__ne__");
//     nodesMap["__ne__"] = __ne__;
//     opNodesMap["!="] = __ne__;
//     __and__ = createNodeAs<DRefLoad>("__and__");
//     nodesMap["__and__"] = __and__;
//     opNodesMap["&&"] = __and__;
//     __or__ = createNodeAs<DRefLoad>("__or__");
//     nodesMap["__or__"] = __or__;
//     opNodesMap["||"] = __or__;

//     __not__ = createNodeAs<DRefLoad>("__not__");
//     nodesMap["__not__"] = __not__;
//     opNodesMap["!"] = __not__;
//     __neg__ = createNodeAs<DRefLoad>("__neg__");
//     nodesMap["__neg__"] = __neg__;
//     opNodesMap["-"] = __neg__;
//     __rev__ = createNodeAs<DRefLoad>("__rev__");
//     nodesMap["__rev__"] = __rev__;
//     opNodesMap["~"] = __rev__;
// }

// } // namespace GraphConstructTree::InnerFuncDRefNodes

// inline node_ptr_t reparent(node_ptr_t &node, node_ptr_t &parent) {
//     *parent << node;
//     return parent;
// }

// inline node_ptr_t linkFunc(node_ptr_t &argsNode, node_ptr_t &funcNode) {
//     node_ptr_t linkNode = createNodeAs<LinkLoad>();
//     *linkNode << argsNode << funcNode;
//     return linkNode;
// }

// data_ptr_t Constructor::extractStaticData(const node_ptr_t &node) {
//     if (node->type() == NodeType::DATA) {
//         const auto dataNode = dynamic_pointer_cast<DataLoad>(node->load());
//         return dataNode->data();
//     } else if (node->type() == NodeType::DREF) {
//         const auto refNode = dynamic_pointer_cast<DRefLoad>(node->load());
//         return make_shared<RefData>(refNode->ident());
//     } else {
//         return nullptr;
//     }
// }

// pair<node_ptr_t, data_ptr_t> Constructor::makeRefData(const node_ptr_t &expr) {
//     const string indent = to_string(indentIndex_++);
//     node_ptr_t refNode = createNodeAs<NRefLoad>(indent);
//     *refNode << expr;
//     data_ptr_t data = make_shared<RefData>(indent);
//     return make_pair(refNode, data);
// }

// pair<data_ptr_t, bool> Constructor::extractData(const node_ptr_t &node, node_ptr_t &execNode) {
//     const data_ptr_t data = extractStaticData(node);
//     if (data) {
//         return make_pair(data, false);
//     } else {
//         auto [refNode, refData] = makeRefData(node);
//         *execNode << refNode;
//         return make_pair(refData, true);
//     }
// }

// pair<data_ptr_t, bool> Constructor::extractData(const node_ptr_t &node, node_ptr_t &execNode, bool &dangling) {
//     auto [refData, dang] = extractData(node, execNode);
//     if (dang) {
//         dangling = true;
//     }
//     return make_pair(refData, dang);
// }

// /*
// program : SEP? (decl SEP?)* EOF;
// */
// any Constructor::visitProgram(OpenCMLParser::ProgramContext *context) {
//     enter("Program");

//     root_ = createNodeAs<ExecLoad>();

//     const auto &decls = context->decl();

//     if (decls.size() == 1)
//         root_ = any_cast<node_ptr_t>(visitDecl(decls[0]));
//     else {
//         for (const auto &decl : decls) {
//             *root_ << any_cast<node_ptr_t>(visitDecl(decl));
//         }
//     }

//     leave("Program");
//     return root_;
// }

// /*
// decl
//     : moduleDecl
//     | importDecl
//     | exportDecl
//     | dataDecl
//     | funcDecl
//     | typeDecl
//     | useDecl
//     ;
// */
// any Constructor::visitDecl(OpenCMLParser::DeclContext *context) { return visit(context->children[0]); }

// /*
// stmt
//     : dataDecl
//     | funcDecl
//     | typeDecl
//     | dataExpr
//     | useDecl
//     | retStmt
//     | blockStmt
//     ;
// */
// any Constructor::visitStmt(OpenCMLParser::StmtContext *context) {
//     enter("Stmt");
//     any res = visit(context->children[0]);
//     leave("Stmt");
//     return res;
// }

// /*
// stmtList : stmt (SEP? stmt)* SEP? ;
// */
// any Constructor::visitStmtList(OpenCMLParser::StmtListContext *context) {
//     enter("StmtList");
//     pushScope();

//     node_ptr_t execNode = createNodeAs<ExecLoad>();

//     vector<OpenCMLParser::UseDeclContext *> froms;
//     vector<OpenCMLParser::TypeDeclContext *> types;
//     vector<OpenCMLParser::FuncDeclContext *> decls;
//     vector<OpenCMLParser::StmtContext *> stmts;

//     // 1. Classify each statement into the appropriate category. Sequence: Decl > Type > Def >
//     for (const auto &stmt : context->stmt()) {
//         if (stmt->useDecl()) {
//             froms.push_back(stmt->useDecl());
//         } else if (stmt->typeDecl()) {
//             types.push_back(stmt->typeDecl());
//         } else if (stmt->funcDecl()) {
//             decls.push_back(stmt->funcDecl());
//             stmts.push_back(stmt);
//         } else {
//             stmts.push_back(stmt);
//         }
//     }

//     // 2. Process statements in priority order
//     // 2.1 Handle imports first (may affect type resolution)
//     for (const auto &stmt : froms) {
//         *execNode << any_cast<node_ptr_t>(visitUseDecl(stmt));
//     }

//     // 2.2 Process type declarations (prerequisite for functions)
//     for (const auto &stmt : types) {
//         *execNode << any_cast<node_ptr_t>(visitTypeDecl(stmt));
//     }

//     // 2.3 Register function signatures (before their bodies are processed)
//     for (const auto &decl : decls) {
//         func_type_ptr_t funcType = any_cast<func_type_ptr_t>(visitFuncDecl(decl));
//         node_ptr_t declNode = createNodeAs<DeclLoad>(funcType); // Create declaration node
//         *execNode << declNode;                                  // Attach to execution block
//     }

//     // 2.4 Process general statements and function bodies
//     for (const auto &stmt : stmts) {
//         *execNode << any_cast<node_ptr_t>(visitStmt(stmt));
//     }

//     popScope();
//     leave("StmtList");
//     return execNode;
// }

// /*
// moduleDecl : MODULE identDef ;
// */
// any Constructor::visitModuleDecl(OpenCMLParser::ModuleDeclContext *context) { return nullptr; }

// /*
// importDecl : IMPORT (STRING | (identDef | bracedIdents) FROM STRING) ;
// */
// any Constructor::visitImportDecl(OpenCMLParser::ImportDeclContext *context) { return nullptr; }

// /*
// exportDecl : EXPORT (dataDecl | typeDecl | bracedIdents) ;
// */
// any Constructor::visitExportDecl(OpenCMLParser::ExportDeclContext *context) { return nullptr; }

// /*
// blockStmt  : WAIT? stmtBlock ;
// */
// any Constructor::visitBlockStmt(OpenCMLParser::BlockStmtContext *context) { return nullptr; }

// /*
// stmtBlock  : SYNC? '{' stmtList? '}' ;
// */
// any Constructor::visitStmtBlock(OpenCMLParser::StmtBlockContext *context) { return nullptr; }

// /*
// blockExpr  : stmtBlock | dataExpr ;
// */
// any Constructor::visitBlockExpr(OpenCMLParser::BlockExprContext *context) { return nullptr; }

// /*
// funcData   : modifiers? angledParams? parentParams (':' typeExpr)? '=>' blockExpr ;
// */
// any Constructor::visitFuncData(OpenCMLParser::FuncDataContext *context) {}

// /*
// funcDecl   :
//         (WITH angledParams)?
//         EXPORT? implMark? modifiers?
//         FUNC identDef parentParams (':' typeExpr)? stmtBlock ;
// */
// any Constructor::visitFuncDecl(OpenCMLParser::FuncDeclContext *context) {
//     enter("FuncDecl");

//     if (funcDecls_.find(context) != funcDecls_.end()) { // hit memory
//         leave("FuncDecl");
//         return funcDecls_[context];
//     }

//     // TODO: Implement annotations

//     const string ident = any_cast<string>(visitIdentDef(context->identDef()));

//     shared_ptr<FunctorType> funcType = nullptr;
//     const auto withType = make_shared<ParamsType>();
//     const auto paramsType = make_shared<ParamsType>();

//     const auto &typeExpr = context->typeExpr();
//     if (typeExpr) {
//         const auto returnType = any_cast<type_ptr_t>(visitTypeExpr(typeExpr));
//         funcType = make_shared<FunctorType>(std::move(ident), withType, paramsType, returnType);
//     } else {
//         // if no return type is specified, the default return type is void
//         funcType = make_shared<FunctorType>(std::move(ident), withType, paramsType, voidTypePtr);
//     }

//     const auto &angledParams = context->angledParams();
//     if (angledParams) {
//         const auto &pairedParams =
//             any_cast<vector<tuple<string, type_ptr_t, data_ptr_t, bool>>>(visitAngledParams(context->angledParams()));
//         for (const auto &[name, type, data, isVar] : pairedParams) {
//             withType->add(name, type, data);
//             bool success = funcType->addIdent(name, isVar);
//             if (!success) {
//                 const auto &token = context->getStart();
//                 throw BuildException("Identifier '" + name + "' already exists in the function signature", token);
//             }
//         }
//     }

//     const auto &params =
//         any_cast<vector<tuple<string, type_ptr_t, data_ptr_t, bool>>>(visitParentParams(context->parentParams()));
//     for (const auto &[name, type, data, isVar] : params) {
//         paramsType->add(name, type, data);
//         bool success = funcType->addIdent(name, isVar);
//         if (!success) {
//             const auto &token = context->getStart();
//             throw BuildException("Identifier '" + name + "' already exists in the function signature", token);
//         }
//     }

//     const auto &modifiers = context->modifiers();
//     if (modifiers) {
//         const auto &modSet = any_cast<unordered_set<FunctorModifier>>(visitModifiers(modifiers));
//         funcType->setModifiers(modSet);
//         try {
//             funcType->checkModifiers();
//         } catch (const exception &e) {
//             const auto &token = modifiers->getStart();
//             throw BuildException(e.what(), token);
//         }
//     }

//     // note: this node may be shared
//     funcDecls_[context] = funcType;
//     leave("FuncDecl");
//     return funcType;
// }

// /*
// parentIdents  : '(' identList? ','? ')' ;
// */
// any Constructor::visitParentIdents(OpenCMLParser::ParentIdentsContext *context) {
//     enter("ParentIndents");
//     any res;
//     const auto &identList = context->identList();
//     if (identList) {
//         res = visitIdentList(identList);
//     } else {
//         res = vector<string>();
//     }
//     leave("ParentIndents");
//     return res;
// }

// /*
// bracedIdents  : '{' identList? ','? '}' ;
// */
// any Constructor::visitBracedIdents(OpenCMLParser::BracedIdentsContext *context) {
//     enter("BracedIdents");
//     any res;
//     const auto &identList = context->identList();
//     if (identList) {
//         res = visitIdentList(identList);
//     } else {
//         res = vector<string>();
//     }
//     leave("BracedIdents");
//     return res;
// }

// /*
// bracketIdents : '[' identList? ','? ']' ;
// */
// any Constructor::visitBracketIdents(OpenCMLParser::BracketIdentsContext *context) {
//     enter("BracketIdents");
//     any res;
//     const auto &identList = context->identList();
//     if (identList) {
//         res = visitIdentList(identList);
//     } else {
//         res = vector<string>();
//     }
//     leave("BracketIdents");
//     return res;
// }

// /*
// carrier       : identList | parentIdents | bracedIdents | bracketIdents ;
// */
// any Constructor::visitCarrier(OpenCMLParser::CarrierContext *context) {
//     enter("Carrier");
//     const size_t alt = context->getAltNumber();
//     any res;
//     switch (alt) {
//     case 1: // identList
//         res = make_pair(alt, visitIdentList(context->identList()));
//         break;
//     case 2: // parentIdents
//         res = make_pair(alt, visitParentIdents(context->parentIdents()));
//         break;
//     case 3: // bracedIdents
//         res = make_pair(alt, visitBracedIdents(context->bracedIdents()));
//         break;
//     case 4: // bracketIdents
//         res = make_pair(alt, visitBracketIdents(context->bracketIdents()));
//         break;

//     default:
//         throw runtime_error("Unknown carrier type");
//     }
//     leave("Carrier");
//     return res;
// }

// /*
// dataDecl   : (LET | VAR) carrier (':' typeList)? '=' dataList ;
// */
// any Constructor::visitDataDecl(OpenCMLParser::DataDeclContext *context) {}

// /*
// typeDecl   : implMark? TYPE identDef '=' (typeExpr | STRING) ;
// */
// any Constructor::visitTypeDecl(OpenCMLParser::TypeDeclContext *context) {
//     enter("TypeDecl");

//     const string &ident = any_cast<string>(visitIdentDef(context->identDef()));

//     type_ptr_t type;

//     if (context->typeExpr()) {
//         type = any_cast<type_ptr_t>(visitTypeExpr(context->typeExpr()));
//     } else {
//         type = any_cast<type_ptr_t>(context->STRING());
//     }

//     typeScope_->insert(ident, type);
//     node_ptr_t result = createNodeAs<TypeLoad>(type);
//     leave("TypeDecl");
//     return result;
// }

// /*
// useDecl    : USE (identDef '=')? identRef ;
// */
// any Constructor::visitUseDecl(OpenCMLParser::UseDeclContext *context) {
//     enter("UseDecl");
//     string path = "";

//     vector<string> idents;
//     idents.push_back(any_cast<string>(visitIdentRef(context->identRef())));

//     if (context->identDef()) {
//         path = any_cast<string>(visitIdentDef(context->identDef()));
//     }

//     node_ptr_t result = createNodeAs<FromLoad>(path, idents);

//     leave("UseDecl");
//     return result;
// }

// /*
// retStmt    : (RETURN | RAISE | THROW) dataList ;
// */
// any Constructor::visitRetStmt(OpenCMLParser::RetStmtContext *context) {
//     enter("RetStmt");
//     node_ptr_t resNode;

//     if (context->RETURN()) {
//         resNode = createNodeAs<RetnLoad>();
//     } else if (context->RAISE()) {
//         resNode = createNodeAs<ExitLoad>();
//     } else if (context->THROW()) {
//         resNode = createNodeAs<ExitLoad>();
//     } else {
//         throw runtime_error("Unknown return type");
//     }

//     // TODO: ??
//     *resNode << any_cast<node_ptr_t>(context->dataList());

//     leave("RetStmt");
//     return resNode;
// }

// /*
// implMark    : INNER | OUTER ;
// */
// any Constructor::visitImplMark(OpenCMLParser::ImplMarkContext *context) {
//     enter("ImplMark");

//     Type::ImplMark result;
//     if (context->INNER()) {
//         result = Type::ImplMark::INNER;
//     } else if (context->OUTER()) {
//         result = Type::ImplMark::OUTER;
//     } else {
//         result = Type::ImplMark::GRAPH;
//     }

//     leave("ImplMark");

//     return result;
// }

// /*
// modifiers   : (ATOMIC | SHARED | SYNC | MACRO)+ ;
// */
// any Constructor::visitModifiers(OpenCMLParser::ModifiersContext *context) { return nullptr; }

// /*
// indexValue   : '...'? dataExpr ;
// */
// any Constructor::visitIndexValue(OpenCMLParser::IndexValueContext *context) { return nullptr; }

// /*
// keyTypePair  : identDef ':' typeExpr ;
// */
// any Constructor::visitKeyTypePair(OpenCMLParser::KeyTypePairContext *context) { return nullptr; }

// /*
// keyValuePair : identDef ':' dataExpr | '...' dataExpr ;
// */
// any Constructor::visitKeyValuePair(OpenCMLParser::KeyValuePairContext *context) { return nullptr; }

// /*
// keyParamPair : VAR? identDef ':' typeExpr ('=' dataExpr)? ;
// */
// any Constructor::visitKeyParamPair(OpenCMLParser::KeyParamPairContext *context) { return nullptr; }

// /*
// dataList     : dataExpr (',' dataExpr)* ;
// */
// any Constructor::visitDataList(OpenCMLParser::DataListContext *context) { return nullptr; }

// /*
// identList    : identDef (',' identDef)* ;
// */
// any Constructor::visitIdentList(OpenCMLParser::IdentListContext *context) {
//     enter("IdentList");
//     vector<string> identList;
//     for (const auto &ident : context->identDef()) {
//         identList.push_back(ident->getText());
//     }
//     leave("IdentList");
//     return identList;
// }

// /*
// indexValues  : indexValue (',' indexValue)* ;
// */
// any Constructor::visitIndexValues(OpenCMLParser::IndexValuesContext *context) { return nullptr; }

// /*
// pairedValues : keyValuePair (',' keyValuePair)* ;
// */
// any Constructor::visitPairedValues(OpenCMLParser::PairedValuesContext *context) { return nullptr; }

// /*
// pairedParams : keyParamPair (',' keyParamPair)* ;
// */
// any Constructor::visitPairedParams(OpenCMLParser::PairedParamsContext *context) { return nullptr; }

// /*
// argumentList : indexValues (',' pairedValues)? | pairedValues ;
// */
// any Constructor::visitArgumentList(OpenCMLParser::ArgumentListContext *context) { return nullptr; }

// /*
// memberAccess : '[' dataExpr (':' dataExpr (':' dataExpr)?)? ']' ;
// */
// any Constructor::visitMemberAccess(OpenCMLParser::MemberAccessContext *context) { return nullptr; }

// /*
// parentParams : '(' pairedParams? ','? ')' ;
// */
// any Constructor::visitParentParams(OpenCMLParser::ParentParamsContext *context) { return nullptr; }

// /*
// parentArgues : '(' argumentList? ','? ')' ;
// */
// any Constructor::visitParentArgues(OpenCMLParser::ParentArguesContext *context) { return nullptr; }

// /*
// angledParams : '<' pairedParams? ','? '>' ;
// */
// any Constructor::visitAngledParams(OpenCMLParser::AngledParamsContext *context) { return nullptr; }

// /*
// angledValues : '<' argumentList? ','? '>' ;
// */
// any Constructor::visitAngledValues(OpenCMLParser::AngledValuesContext *context) { return nullptr; }

// /*
// pattern
//     : identRef
//     | literal
//     | '(' (dataList | identList)? ','? ')'
//     | '{' (pairedValues | identList)? ','? '}'
//     | '_' // wildcard
//     ;
// */
// any Constructor::visitPattern(OpenCMLParser::PatternContext *context) { return nullptr; }

// /*
// matchCase
//     : CASE pattern ('|' pattern)* '=>' blockExpr
//     ;
// */
// any Constructor::visitMatchCase(OpenCMLParser::MatchCaseContext *context) { return nullptr; }

// /*
// catchClause
//     : CATCH identDef ':' typeExpr stmtBlock
//     ;
// */
// any Constructor::visitCatchClause(OpenCMLParser::CatchClauseContext *context) { return nullptr; }

// /*
// ctrlExpr
//     : IF logicalOrExpr THEN blockExpr (ELSE blockExpr)?
//     | MATCH identRef '{' matchCase+ '}'
//     | TRY stmtBlock catchClause+ (FINALLY stmtBlock)?
//     ;
// */
// any Constructor::visitCtrlExpr(OpenCMLParser::CtrlExprContext *context) { return nullptr; }

// /*
// dataExpr
//     : waitExpr
//     | ctrlExpr
//     ;
// */
// any Constructor::visitDataExpr(OpenCMLParser::DataExprContext *context) { return nullptr; }

// /*
// waitExpr : WAIT? assignExpr ;
// */
// any Constructor::visitWaitExpr(OpenCMLParser::WaitExprContext *context) { return nullptr; }

// /*
// assignExpr
//     : logicalOrExpr (('=' | '+=' | '-=' | '*=' | '/=' | '%=' | '^=' | '@=' | '&=' | '|=') logicalOrExpr)?
//     ;
// */
// any Constructor::visitAssignExpr(OpenCMLParser::AssignExprContext *context) { return nullptr; }

// /*
// logicalOrExpr
//     : logicalAndExpr ('||' logicalAndExpr)*
//     ;
// */
// any Constructor::visitLogicalOrExpr(OpenCMLParser::LogicalOrExprContext *context) { return nullptr; }

// /*
// logicalAndExpr
//     : equalityExpr ('&&' equalityExpr)*
//     ;
// */
// any Constructor::visitLogicalAndExpr(OpenCMLParser::LogicalAndExprContext *context) { return nullptr; }

// /*
// equalityExpr
//     : relationalExpr (('===' | '!==' | '==' | '!=') relationalExpr)*
//     ;
// */
// any Constructor::visitEqualityExpr(OpenCMLParser::EqualityExprContext *context) { return nullptr; }

// /*
// relationalExpr
//     : additiveExpr (('<' | '>' | '<=' | '>=') additiveExpr)*
//     ;
// */
// any Constructor::visitRelationalExpr(OpenCMLParser::RelationalExprContext *context) { return nullptr; }

// /*
// additiveExpr
//     : multiplicativeExpr (('+' | '-') multiplicativeExpr)*
//     ;
// */
// any Constructor::visitAdditiveExpr(OpenCMLParser::AdditiveExprContext *context) { return nullptr; }

// /*
// multiplicativeExpr
//     : nullableExpr (('*' | '/' | '^' | '@' | '%') nullableExpr)*
//     ;
// */
// any Constructor::visitMultiplicativeExpr(OpenCMLParser::MultiplicativeExprContext *context) { return nullptr; }

// /*
// nullableExpr
//     : unaryExpr (('??' | '!!') dataExpr)?
//     ;
// */
// any Constructor::visitNullableExpr(OpenCMLParser::NullableExprContext *context) { return nullptr; }

// /*
// unaryExpr
//     : linkExpr ((AS | IS) typeExpr)?
//     | ('!' | '-' | '~') linkExpr
//     ;
// */
// any Constructor::visitUnaryExpr(OpenCMLParser::UnaryExprContext *context) { return nullptr; }

// /*
// linkExpr
//     : bindExpr (('->' | '?->') bindExpr)*
//     ;
// */
// any Constructor::visitLinkExpr(OpenCMLParser::LinkExprContext *context) { return nullptr; }

// /*
// bindExpr
//     : annoExpr (('..' | '?..') annoExpr)*
//     ;
// */
// any Constructor::visitBindExpr(OpenCMLParser::BindExprContext *context) { return nullptr; }

// /*
// annoExpr
//     : withExpr ({isAdjacent()}? (memberAccess | parentArgues | angledValues | '!'))*
//     ;
// */
// any Constructor::visitAnnoExpr(OpenCMLParser::AnnoExprContext *context) { return nullptr; }

// /*
// withExpr
//     : primaryData (('.' | '?.') primaryData)*
//     ;
// */
// any Constructor::visitWithExpr(OpenCMLParser::WithExprContext *context) {
//     // TODO: ?.
//     enter("WithExpr");
//     const auto &dataList = context->primaryData();
//     node_ptr_t lhsNode = any_cast<node_ptr_t>(visitPrimaryData(dataList[0]));

//     for (size_t i = 1; i < dataList.size(); ++i) {
//         node_ptr_t execNode = createNodeAs<ExecLoad>();
//         node_ptr_t rhsNode = any_cast<node_ptr_t>(visitPrimaryData(dataList[i]));

//         node_ptr_t withNode = createNodeAs<WithLoad>();
//         *withNode << lhsNode << rhsNode;

//         lhsNode = withNode;
//     }

//     leave("WithExpr");
//     return lhsNode;
// }

// /*
// dictData
//     : '{' (pairedValues ','?)? '}' // no list comprehension because the struct of dict is immutable
//     ;
// */
// any Constructor::visitDictData(OpenCMLParser::DictDataContext *context) {}

// /*
// listData
//     : '[' ((indexValues ','?) | dataExpr FOR identRef IN dataExpr (IF dataExpr)?)? ']'
//     ;
// */
// any Constructor::visitListData(OpenCMLParser::ListDataContext *context) {}

// /*
// tupleData
//     : '(' dataList? ','? ')'
//     ;
// */
// any Constructor::visitTupleData(OpenCMLParser::TupleDataContext *context) {}

// /*
// primaryData
//     : identRef
//     | literal
//     | listData
//     | dictData
//     | '(' dataExpr ')'
//     | tupleData
//     | funcData
//     ;
// */
// any Constructor::visitPrimaryData(OpenCMLParser::PrimaryDataContext *context) { return nullptr; }

// /*
// literal
//     : INTEGER
//     | REAL
//     | STRING
//     | MULTI_STR
//     | FSTRING
//     | TRUE
//     | FALSE
//     | NULL
//     ;
// */
// any Constructor::visitLiteral(OpenCMLParser::LiteralContext *context) {
//     enter("Literal: " + to_string(context->getAltNumber()));
//     data_ptr_t data = nullptr;

//     switch (context->getAltNumber()) {
//     case 1: // INTEGER
//         data = dynamic_pointer_cast<Data>(
//             make_shared<PrimaryData<int64_t>>(parseNumber<int64_t>(context->INTEGER()->getText())));
//         break;
//     case 2: // REAL
//         data = dynamic_pointer_cast<Data>(
//             make_shared<PrimaryData<double>>(parseNumber<double>(context->REAL()->getText())));
//         break;
//     case 3: // STRING
//     {
//         const auto &text = context->STRING()->getText();
//         data = dynamic_pointer_cast<Data>(make_shared<StringData>(text.substr(1, text.size() - 2)));
//     } break;
//     case 4: // MULTI_STR
//     {
//         const auto &text = context->MULTI_STR()->getText();
//         data = dynamic_pointer_cast<Data>(make_shared<StringData>(text.substr(3, text.size() - 6)));
//     } break;
//     case 5: // FSTRING
//     {
//         // TODO: Implement FSTRING
//         const auto &text = context->FSTRING()->getText();
//         data = dynamic_pointer_cast<Data>(make_shared<StringData>(text.substr(2, text.size() - 3)));
//     } break;
//     case 6: // TRUE
//         data = dynamic_pointer_cast<Data>(make_shared<PrimaryData<bool>>(true));
//         break;
//     case 7: // FALSE
//         data = dynamic_pointer_cast<Data>(make_shared<PrimaryData<bool>>(false));
//         break;
//     case 8: // NULL
//         data = dynamic_pointer_cast<Data>(make_shared<NullData>());
//         break;

//     default:
//         break;
//     }

//     any res = createNodeAs<DataLoad>(data);
//     leave("Literal");
//     return res;
// }

// /*
// typeExpr
//     : unionType ('?' unionType?)?
//     ;
// */
// any Constructor::visitTypeExpr(OpenCMLParser::TypeExprContext *context) { return nullptr; }

// /*
// unionType
//     : interType ('|' interType)*
//     ;
// */
// any Constructor::visitUnionType(OpenCMLParser::UnionTypeContext *context) { return nullptr; }

// /*
// interType
//     : diffType ('&' diffType)*
//     ;
// */
// any Constructor::visitInterType(OpenCMLParser::InterTypeContext *context) {}

// /*
// diffType
//     : keyUnionDiffType ('\\' keyUnionDiffType)*
//     ;
// */
// any Constructor::visitDiffType(OpenCMLParser::DiffTypeContext *context) {}

// /*
// keyUnionDiffType
//     : keyInterType (('+' | '-') keyInterType)*
//     ;
// */
// any Constructor::visitKeyUnionDiffType(OpenCMLParser::KeyUnionDiffTypeContext *context) {}

// /*
// keyInterType
//     : typeUnit ('^' typeUnit)*
//     ;
// */
// any Constructor::visitKeyInterType(OpenCMLParser::KeyInterTypeContext *context) {}

// /*
// typeUnit : (identDef OF)? listType ;
// */
// any Constructor::visitTypeUnit(OpenCMLParser::TypeUnitContext *context) {}

// /*
// listType
//     : specType ('[' ']')*
//     ;
// */
// any Constructor::visitListType(OpenCMLParser::ListTypeContext *context) { return nullptr; }

// /*
// typeOrData : typeExpr | CONST dataExpr ;
// */
// any Constructor::visitTypeOrData(OpenCMLParser::TypeOrDataContext *context) { return visit(context->children[0]); }

// /*
// specType
//     : primaryType ('<' typeOrData (',' typeOrData)* '>')?
//     ;
// */
// any Constructor::visitSpecType(OpenCMLParser::SpecTypeContext *context) {}

// /*
// primaryType
//     : INNER_ATOM_TYPE
//     | dictType
//     | identRef
//     | '(' typeExpr ')'
//     | tupleType
//     | funcType
//     | TYPEOF dataExpr
//     | TYPEAS identDef
//     ;
// */
// any Constructor::visitPrimaryType(OpenCMLParser::PrimaryTypeContext *context) { return nullptr; }

// /*
// dictType
//     : '{' (keyTypePair (',' keyTypePair)*)? ','? '}'
//     ;
// */
// any Constructor::visitDictType(OpenCMLParser::DictTypeContext *context) { return nullptr; }

// /*
// typeList
//     : typeExpr (',' typeExpr)*
//     ;
// */
// any Constructor::visitTypeList(OpenCMLParser::TypeListContext *context) {
//     enter("TypeList");
//     vector<type_ptr_t> typeList;
//     for (const auto &type : context->typeExpr()) {
//         typeList.push_back(any_cast<type_ptr_t>(visitTypeExpr(type)));
//     }
//     leave("TypeList");
//     return typeList;
// }

// /*
// tupleType
//     : '(' typeList? ','? ')'
//     ;
// */
// any Constructor::visitTupleType(OpenCMLParser::TupleTypeContext *context) { return nullptr; }

// /*
// funcType
//     : modifiers? angledParams? parentParams '=>' typeExpr
//     ;
// */
// any Constructor::visitFuncType(OpenCMLParser::FuncTypeContext *context) {}

// /*
// identDef : IDENTIFIER ;
// */
// any Constructor::visitIdentDef(OpenCMLParser::IdentDefContext *context) { return nullptr; }

// /*
// identRef : (IDENTIFIER '::')* IDENTIFIER ;
// */
// any Constructor::visitIdentRef(OpenCMLParser::IdentRefContext *context) { return nullptr; }
