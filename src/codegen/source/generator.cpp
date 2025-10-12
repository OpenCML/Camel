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
 * Created: Sep. 13, 2025
 * Updated: Oct. 12, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "generator.h"
#include <string>

using namespace AbstractSyntaxTree;
using namespace std;
using namespace ASTCodeGen;

const char *typeName(int t) {
    switch (t) {
    case 0:
        return "Module";
    case 1:
        return "Import";
    case 2:
        return "Export";
    case 3:
        return "Stmt";
    case 4:
        return "Data";
    case 5:
        return "Type";
    case 6:
        return "NamedData";
    case 7:
        return "NamedType";
    case 8:
        return "NamedPair";
    case 9:
        return "Optional";
    case 10:
        return "Repeated";
    default:
        return "Unknown";
    }
}

string Generator::generate(const AbstractSyntaxTree::node_ptr_t &ast) {
    if (!ast)
        return "";
    return generateNode(ast);
}

string Generator::generateNode(const AbstractSyntaxTree::node_ptr_t &node) {
    if (!node)
        return "";

    // cout << "DEBUG: generateNode processing node with LoadType: "<<
    // static_cast<int>(node->load()->type()) << endl;
    switch (node->load()->type()) {
    case LoadType::Module:
        return generateModuleLoad(node);

    case LoadType::Import:
        return generateImportLoad(node);

    case LoadType::Export:
        return generateExportLoad(node);

    case LoadType::Stmt:
        return generateStmtLoad(node);

    case LoadType::Data:
        //  cout << "DEBUG: generateNode calling generateDataLoad for Data node" << endl;
        return generateDataLoad(node);

    case LoadType::Type:
        return generateTypeLoad(node);

    case LoadType::NamedData:
        return generateNamedDataLoad(node);

    case LoadType::NamedType:
        return generateNamedTypeLoad(node);

    case LoadType::NamedPair:
        return generateNamedPairLoad(node);

    case LoadType::Repeated:
        //    cout << "DEBUG: generateNode calling generateChildren for Repeated node" <<
        //    endl;
        return generateChildren(node);

    case LoadType::Optional:
        //    cout << "DEBUG: generateNode calling generateNullableTypeLoad for Optional node" <<
        //    endl;
        return generateNullableTypeLoad(node);

    default:
        //  cout << "DEBUG: generateNode calling generateChildren for default case, LoadType: " <<
        //  static_cast<int>(node->load()->type()) << endl;
        return generateChildren(node);
    }
}

string Generator::generateChildren(const AbstractSyntaxTree::node_ptr_t &node) {
    if (!node)
        return "";

    // cout << "DEBUG: generateChildren processing node with " << node->size() << " children" <<
    // endl;
    string code;
    for (size_t i = 0; i < node->size(); ++i) {
        auto child = node->at(i);
        if (child) {
            auto childNode = static_pointer_cast<AbstractSyntaxTree::Node>(child);
            // cout << "DEBUG: generateChildren processing child " << i << " with LoadType: " <<
            // static_cast<int>(childNode->load()->type()) << endl;
            string childCode = generateNode(childNode);
            if (!childCode.empty()) {
                code += childCode;
            }
        }
    }
    return code;
}

string Generator::generateModuleLoad(const AbstractSyntaxTree::node_ptr_t &node) {
    if (!node)
        return "";
    auto moduleLoad = dynamic_pointer_cast<AbstractSyntaxTree::ModuleLoad>(node->load());
    if (!moduleLoad)
        return "";

    string geneCode = moduleLoad->geneCode();
    if (geneCode.empty() || geneCode == "module ") {
        return generateChildren(node);
    }
    return geneCode + ";\n" + generateChildren(node);
}

string Generator::generateImportLoad(const AbstractSyntaxTree::node_ptr_t &node) {
    if (!node)
        return "";
    auto importLoad = dynamic_pointer_cast<AbstractSyntaxTree::ImportLoad>(node->load());
    if (!importLoad)
        return "";
    return importLoad->geneCode() + ";\n" + generateChildren(node);
}

string Generator::generateExportLoad(const AbstractSyntaxTree::node_ptr_t &node) {
    if (!node)
        return "";
    auto exportLoad = dynamic_pointer_cast<AbstractSyntaxTree::ExportLoad>(node->load());
    if (!exportLoad)
        return "";
    return exportLoad->geneCode() + ";\n" + generateChildren(node);
}

string Generator::generateDataDeclLoad(const AbstractSyntaxTree::node_ptr_t &node) {
    auto dataDeclLoad = dynamic_pointer_cast<AbstractSyntaxTree::DataDeclLoad>(node->load());
    if (!dataDeclLoad)
        return "";

    string code = dataDeclLoad->isVar() ? "var " : "let ";

    const auto &refs = dataDeclLoad->refs();
    if (!refs.empty()) {
        code += refs[0].toString();
    }

    string typeCode;
    if (node->size() > 0) {
        auto typeNode = node->at(0);
        if (typeNode) {
            typeCode = generateNode(static_pointer_cast<AbstractSyntaxTree::Node>(typeNode));
            if (!typeCode.empty()) {
                code += ": " + typeCode;
            }
        }
    }

    string valueCode;
    if (node->size() > 1) {
        auto valueNode = node->at(1);
        if (valueNode) {
            valueCode = generateNode(static_pointer_cast<AbstractSyntaxTree::Node>(valueNode));
            if (!valueCode.empty()) {
                code += " = " + valueCode;
            }
        }
    }
    code += ";\n";
    return code;
}

string Generator::extractReturnType(const AbstractSyntaxTree::node_ptr_t &node) {
    if (!node)
        return "";

    if (node->load()->type() == AbstractSyntaxTree::LoadType::Type) {
        auto typeLoad = dynamic_pointer_cast<AbstractSyntaxTree::TypeLoad>(node->load());
        if (typeLoad) {
            auto ttype = typeLoad->typeType();

            if (ttype == AbstractSyntaxTree::TypeType::Func) {
                // cout << "[extractReturnType] FuncType has " << node->size() << " children" <<
                // endl;

                for (size_t i = 0; i < node->size(); ++i) {
                    auto child = node->at(i);
                    if (child) {
                        auto childNode = static_pointer_cast<AbstractSyntaxTree::Node>(child);
                        // cout << "[extractReturnType] FuncType child " << i
                        //     << " type: " << int(childNode->load()->type()) << endl;

                        string typeStr = generateNode(childNode);
                        if (!typeStr.empty() && typeStr != "()") {
                            // cout << "[extractReturnType] Found return type: " << typeStr << endl;
                            return typeStr;
                        }
                    }
                }
                return "";
            }

            if (ttype == AbstractSyntaxTree::TypeType::Ref ||
                ttype == AbstractSyntaxTree::TypeType::Data ||
                ttype == AbstractSyntaxTree::TypeType::Unit ||
                ttype == AbstractSyntaxTree::TypeType::Infer) {
                return generateNode(node);
            }
        }
    }

    for (size_t i = 0; i < node->size(); ++i) {
        auto child = node->at(i);
        if (child) {
            auto childNode = static_pointer_cast<AbstractSyntaxTree::Node>(child);
            string result = extractReturnType(childNode);
            if (!result.empty())
                return result;
        }
    }
    return "";
}

void Generator::collectParamsFromNode(
    const AbstractSyntaxTree::node_ptr_t &node, vector<string> &params, Generator *gen) {
    if (!node)
        return;

    // cout << "[collectParamsFromNode] node type: " << int(node->load()->type()) << " ("
    //     << typeName(int(node->load()->type())) << ")" << endl;

    if (node->load()->type() == AbstractSyntaxTree::LoadType::Repeated ||
        node->load()->type() == AbstractSyntaxTree::LoadType::Optional) {
        // cout << "[collectParamsFromNode] found container node, checking " << node->size() << "
        // children..." << endl;
        for (size_t i = 0; i < node->size(); ++i) {
            auto child = node->at(i);
            if (child) {
                auto childNode = static_pointer_cast<AbstractSyntaxTree::Node>(child);
                collectParamsFromNode(childNode, params, gen);
            }
        }
        return;
    }

    if (node->load()->type() == AbstractSyntaxTree::LoadType::NamedPair) {
        auto namedPairLoad = dynamic_pointer_cast<AbstractSyntaxTree::NamedPairLoad>(node->load());
        if (namedPairLoad) {
            string paramName = namedPairLoad->geneCode();
            string paramType = "";

            if (node->size() > 0) {
                auto child = node->at(0);
                if (child) {
                    auto childNode = static_pointer_cast<AbstractSyntaxTree::Node>(child);
                    paramType = gen->generateNode(childNode);
                }
            }

            string paramCode = paramName;
            if (!paramType.empty()) {
                paramCode += ": " + paramType;
            }

            // cout << "[collectParamsFromNode] found NamedPair: " << paramCode << endl;
            params.push_back(paramCode);
        }
        return;
    }

    if (node->load()->type() == AbstractSyntaxTree::LoadType::NamedType) {
        // cout << "[collectParamsFromNode] found NamedType node, checking " << node->size()   << "
        // children..." << endl;

        for (size_t i = 0; i < node->size(); ++i) {
            auto child = node->at(i);
            if (child) {
                auto childNode = static_pointer_cast<AbstractSyntaxTree::Node>(child);
                collectParamsFromNode(childNode, params, gen);
            }
        }
        return;
    }

    //    cout << "[collectParamsFromNode] found other node type, checking " << node->size()   << "
    //    children..." << endl;
    for (size_t i = 0; i < node->size(); ++i) {
        auto child = node->at(i);
        if (child) {
            auto childNode = static_pointer_cast<AbstractSyntaxTree::Node>(child);
            collectParamsFromNode(childNode, params, gen);
        }
    }
}

void Generator::collectParams(
    const AbstractSyntaxTree::node_ptr_t &node, vector<string> &params, Generator *gen) {
    if (!node)
        return;

    // cout << "[collectParams] node type: " << int(node->load()->type()) << " ("   <<
    // typeName(int(node->load()->type())) << ")" << endl;

    if (node->load()->type() == AbstractSyntaxTree::LoadType::Repeated ||
        node->load()->type() == AbstractSyntaxTree::LoadType::Optional) {
        // cout << "[collectParams] found container node, checking " << node->size() << "
        // children..." << endl;
        for (size_t i = 0; i < node->size(); ++i) {
            auto child = node->at(i);
            if (child) {
                auto childNode = static_pointer_cast<AbstractSyntaxTree::Node>(child);
                //        cout << "[collectParams] container child " << i << " type: " <<
                //        int(childNode->load()->type()) << " (" <<
                //        typeName(int(childNode->load()->type())) << ")" << endl;
                collectParams(childNode, params, gen);
            }
        }
        return;
    }

    if (node->load()->type() == AbstractSyntaxTree::LoadType::NamedType) {
        // cout << "[collectParams] found NamedType node, checking " << node->size() << "
        // children..."  << endl;

        string paramName = "";
        string paramType = "";

        for (size_t i = 0; i < node->size(); ++i) {
            auto child = node->at(i);
            if (!child)
                continue;

            auto childNode = static_pointer_cast<AbstractSyntaxTree::Node>(child);
            // cout << "[collectParams] NamedType child " << i
            //      << " type: " << int(childNode->load()->type()) << " ("
            //     << typeName(int(childNode->load()->type())) << ")" << endl;

            if (childNode->load()->type() == AbstractSyntaxTree::LoadType::Type) {
                paramType = gen->generateNode(childNode);
                //    cout << "[collectParams] Got paramType from Type child: " << paramType <<
                //    endl;
            } else if (childNode->load()->type() == AbstractSyntaxTree::LoadType::NamedPair) {
                auto namedPairLoad =
                    dynamic_pointer_cast<AbstractSyntaxTree::NamedPairLoad>(childNode->load());
                if (namedPairLoad) {
                    paramName = namedPairLoad->geneCode();
                    //   cout << "[collectParams] Got paramName from NamedPair: " << paramName <<
                    //   endl;
                }
            } else if (childNode->load()->type() == AbstractSyntaxTree::LoadType::Repeated) {
                // cout << "[collectParams] Found Repeated node in NamedType, checking "
                //      << childNode->size() << " children..." << endl;
                // cout << "[collectParams] Repeated has " << childNode->size() << " children" <<
                // endl;
                for (size_t j = 0; j < childNode->size(); ++j) {
                    auto repeatedChild = childNode->at(j);
                    if (!repeatedChild)
                        continue;

                    auto repeatedChildNode =
                        static_pointer_cast<AbstractSyntaxTree::Node>(repeatedChild);
                    //  cout << "[collectParams] Repeated child " << j
                    //       << " type: " << int(repeatedChildNode->load()->type()) << " ("
                    //       << typeName(int(repeatedChildNode->load()->type())) << ")" << endl;

                    if (repeatedChildNode->load()->type() ==
                        AbstractSyntaxTree::LoadType::NamedPair) {
                        auto namedPairLoad =
                            dynamic_pointer_cast<AbstractSyntaxTree::NamedPairLoad>(
                                repeatedChildNode->load());
                        if (namedPairLoad) {
                            paramName = namedPairLoad->geneCode();
                            //    cout << "[collectParams] Got paramName from NamedPair in Repeated:
                            //    "
                            //         << paramName << endl;
                        }
                    }
                }
            } else if (node->size() == 1) {
                auto childNode = static_pointer_cast<AbstractSyntaxTree::Node>(child);
                collectParams(childNode, params, gen);
            }
            return;
        }

        if (!paramName.empty()) {
            string paramStr = paramName;
            if (!paramType.empty()) {
                paramStr += ": " + paramType;
            }
            params.push_back(paramStr);
            // cout << "[collectParams] Added parameter: " << paramStr << endl;
        }

        if (node->load()->type() == AbstractSyntaxTree::LoadType::Type) {
            auto typeLoad = dynamic_pointer_cast<AbstractSyntaxTree::TypeLoad>(node->load());
            if (typeLoad) {
                // cout << "[collectParams] found Type node, typeType: " <<
                // int(typeLoad->typeType())
                //      << endl;

                if (typeLoad->typeType() == AbstractSyntaxTree::TypeType::Func) {
                    //   cout << "[collectParams] found FuncType, checking " << node->size()
                    //       << " children for parameters..." << endl;
                    for (size_t i = 0; i < node->size(); ++i) {
                        auto child = node->at(i);
                        if (child) {
                            auto childNode = static_pointer_cast<AbstractSyntaxTree::Node>(child);
                            //   cout << "[collectParams] FuncType child " << i
                            //        << " type: " << int(childNode->load()->type()) << " ("
                            //        << typeName(int(childNode->load()->type())) << ")" << endl;
                            collectParams(childNode, params, gen);
                        }
                    }
                    return;
                }

                for (size_t i = 0; i < node->size(); ++i) {
                    auto child = node->at(i);
                    if (child) {
                        auto childNode = static_pointer_cast<AbstractSyntaxTree::Node>(child);
                        collectParams(childNode, params, gen);
                    }
                }
            }
            return;
        }

        // cout << "[collectParams] found other node type, checking " << node->size() << "
        // children..."
        //      << endl;
        for (size_t i = 0; i < node->size(); ++i) {
            auto child = node->at(i);
            if (child) {
                auto childNode = static_pointer_cast<AbstractSyntaxTree::Node>(child);
                collectParams(childNode, params, gen);
            }
        }
    }
}

string Generator::generateFuncDeclLoad(const AbstractSyntaxTree::node_ptr_t &node) {
    if (!node)
        return "";
    auto funcDeclLoad = dynamic_pointer_cast<AbstractSyntaxTree::FuncDeclLoad>(node->load());
    if (!funcDeclLoad)
        return "";

    string code = "";
    string funcName = funcDeclLoad->geneCode();
    // cout << "[generateFuncDeclLoad]原始funcName: " << funcName << endl;
    string withParamsCode, paramsCode, returnTypeCode, bodyCode;
    bool hasWithParams = false;
    bool isMacro = false, isAtomic = false, isShared = false, isSync = false;
    string sImplMark = "";
    for (size_t i = 0; i < node->size(); ++i) {
        auto child = node->at(i);
        if (!child)
            continue;
        auto childNode = static_pointer_cast<AbstractSyntaxTree::Node>(child);

        if (childNode->load()->type() == AbstractSyntaxTree::LoadType::Data &&
            dynamic_pointer_cast<AbstractSyntaxTree::FuncDataLoad>(childNode->load())) {

            for (size_t j = 0; j < childNode->size(); ++j) {
                auto funcDataChild = childNode->at(j);
                if (!funcDataChild)
                    continue;
                auto funcDataChildNode =
                    static_pointer_cast<AbstractSyntaxTree::Node>(funcDataChild);

                if (funcDataChildNode->load()->type() == AbstractSyntaxTree::LoadType::Type &&
                    dynamic_pointer_cast<AbstractSyntaxTree::FuncTypeLoad>(
                        funcDataChildNode->load())) {

                    auto funcTypeLoad = dynamic_pointer_cast<AbstractSyntaxTree::FuncTypeLoad>(
                        funcDataChildNode->load());
                    if (funcTypeLoad) {
                        const auto &modifiers = funcTypeLoad->modifiers();
                        isMacro = modifiers.has(Modifier::Macro);
                        isAtomic = modifiers.has(Modifier::Atomic);
                        isShared = modifiers.has(Modifier::Shared);
                        isSync = modifiers.has(Modifier::Sync);
                        // cout << "[generateFuncDeclLoad] FuncType modifiers: " <<
                        // string(modifiers)
                        //      << ", isMacro: " << isMacro << ", isAtomic: " << isAtomic
                        //      << ", isShared: " << isShared << ", isSync: " << isSync << endl;
                        const auto implMark = funcTypeLoad->implMark();
                        if (implMark == ImplMark::Inner)
                            sImplMark = "inner ";
                        else if (implMark == ImplMark::Outer)
                            sImplMark = "outer ";
                    }

                    vector<string> withParamsVec;
                    vector<string> normalParamsVec;

                    // cout << "[generateFuncDeclLoad] FuncType has " << funcDataChildNode->size()
                    //      << " children" << endl;

                    for (size_t k = 0; k < funcDataChildNode->size(); ++k) {
                        auto funcTypeChild = funcDataChildNode->at(k);
                        if (!funcTypeChild)
                            continue;
                        auto funcTypeChildNode =
                            static_pointer_cast<AbstractSyntaxTree::Node>(funcTypeChild);

                        // cout << "[generateFuncDeclLoad] FuncType child " << k
                        //      << " type: " << int(funcTypeChildNode->load()->type()) << " ("
                        //      << typeName(int(funcTypeChildNode->load()->type())) << ")" << endl;

                        if (k == 0) {
                            collectParamsFromNode(funcTypeChildNode, withParamsVec, this);
                            if (!withParamsVec.empty()) {
                                hasWithParams = true;
                            }
                        } else if (k == 1) {
                            collectParamsFromNode(funcTypeChildNode, normalParamsVec, this);
                        } else {
                            string returnType = extractReturnType(funcTypeChildNode);
                            if (!returnType.empty()) {
                                returnTypeCode = returnType;
                            }
                        }
                    }

                    for (const auto &p : withParamsVec) {
                        if (!withParamsCode.empty())
                            withParamsCode += ", ";
                        withParamsCode += p;
                    }

                    for (const auto &p : normalParamsVec) {
                        if (!paramsCode.empty())
                            paramsCode += ", ";
                        paramsCode += p;
                    }

                } else if (
                    funcDataChildNode->load()->type() == AbstractSyntaxTree::LoadType::Stmt &&
                    dynamic_pointer_cast<AbstractSyntaxTree::StmtBlockLoad>(
                        funcDataChildNode->load())) {
                    bodyCode = generateNode(funcDataChildNode);
                }
            }
        }
    }

    if (hasWithParams) {
        code += "with <" + withParamsCode + ">\n";
    }
    code += sImplMark;
    if (isMacro) {
        code += "macro ";
    }
    if (isAtomic) {
        code += "atomic ";
    }
    if (isShared) {
        code += "shared ";
    }
    if (isSync) {
        code += "sync ";
    }

    code += funcName;
    code += "(" + paramsCode + ")";

    if (!returnTypeCode.empty()) {
        code += ": " + returnTypeCode;
    }

    if (!bodyCode.empty()) {
        code += " " + bodyCode;
    } else {
        code += " {}";
    }
    return code + "\n";
}

string Generator::generateFuncTypeLoad(const AbstractSyntaxTree::node_ptr_t &node) {
    if (!node)
        return "";
    auto funcTypeLoad = dynamic_pointer_cast<AbstractSyntaxTree::FuncTypeLoad>(node->load());
    if (!funcTypeLoad)
        return "";

    string code = "";

    if (node->size() >= 2) {
        auto paramsNode = node->at(0);
        auto returnTypeNode = node->at(1);

        string paramsCode = "";
        if (paramsNode) {
            auto paramsAstNode = static_pointer_cast<AbstractSyntaxTree::Node>(paramsNode);
            paramsCode = generateNode(paramsAstNode);
        }

        string returnTypeCode = "";
        if (returnTypeNode) {
            auto returnTypeAstNode = static_pointer_cast<AbstractSyntaxTree::Node>(returnTypeNode);
            returnTypeCode = generateNode(returnTypeAstNode);
        }

        if (!paramsCode.empty() && !returnTypeCode.empty()) {
            code = "(" + paramsCode + ") => " + returnTypeCode;
        } else if (!paramsCode.empty()) {
            code = "(" + paramsCode + ") => void";
        } else if (!returnTypeCode.empty()) {
            code = "() => " + returnTypeCode;
        } else {
            code = "() => void";
        }
    } else if (node->size() == 1) {
        auto childNode = node->at(0);
        if (childNode) {
            auto childAstNode = static_pointer_cast<AbstractSyntaxTree::Node>(childNode);
            string childCode = generateNode(childAstNode);
            if (!childCode.empty()) {
                if (childCode.find(',') != string::npos || childCode.find(':') != string::npos) {
                    code = "(" + childCode + ") => void";
                } else {
                    code = "() => " + childCode;
                }
            }
        }
    } else {
        code = "() => void";
    }

    return code;
}

string Generator::generateTypeDeclLoad(const AbstractSyntaxTree::node_ptr_t &node) {
    if (!node)
        return "";
    auto typeDeclLoad = dynamic_pointer_cast<AbstractSyntaxTree::TypeDeclLoad>(node->load());
    if (!typeDeclLoad)
        return "";

    string code = typeDeclLoad->geneCode();

    if (node->size() > 0) {
        auto firstChild = node->at(0);
        if (firstChild) {
            auto firstChildNode = static_pointer_cast<AbstractSyntaxTree::Node>(firstChild);

            if (code.find("inner") != string::npos) {
                string typeExpr = generateNode(firstChildNode);
                code = "inner type " + code.substr(6);
                code += " = " + typeExpr;
            } else {
                string childrenCode = generateNode(firstChildNode);
                if (!childrenCode.empty()) {
                    if (childrenCode.find("=>") != string::npos) {
                        code += " " + childrenCode;
                    } else {
                        code += " = " + childrenCode;
                    }
                }
            }
        }
    }

    return code + ";\n";
}

string Generator::generateNameDeclLoad(const AbstractSyntaxTree::node_ptr_t &node) {
    if (!node)
        return "";
    auto nameDeclLoad = dynamic_pointer_cast<AbstractSyntaxTree::NameDeclLoad>(node->load());
    if (!nameDeclLoad)
        return "";

    string code = nameDeclLoad->geneCode();
    code += generateChildren(node);
    return code + ";\n";
}

string Generator::generateStmtLoad(const AbstractSyntaxTree::node_ptr_t &node) {
    if (!node)
        return "";

    auto stmtLoad = dynamic_pointer_cast<AbstractSyntaxTree::StmtLoad>(node->load());
    if (!stmtLoad)
        return "";

    switch (stmtLoad->stmtType()) {
    case AbstractSyntaxTree::StmtType::Data:
        return generateDataDeclLoad(node);
    case AbstractSyntaxTree::StmtType::Func:
        return generateFuncDeclLoad(node);
    case AbstractSyntaxTree::StmtType::Type:
        return generateTypeDeclLoad(node);
    case AbstractSyntaxTree::StmtType::Name:
        return generateNameDeclLoad(node);
    case AbstractSyntaxTree::StmtType::Expr:
        return generateExprStmtLoad(node);
    case AbstractSyntaxTree::StmtType::Exit:
        return generateExitStmtLoad(node);
    case AbstractSyntaxTree::StmtType::Block:
        return generateStmtBlockLoad(node);
    default:
        return generateChildren(node);
    }
}

string Generator::generateExitStmtLoad(const AbstractSyntaxTree::node_ptr_t &node) {
    if (!node)
        return "";
    auto exitStmtLoad = dynamic_pointer_cast<AbstractSyntaxTree::ExitStmtLoad>(node->load());
    if (!exitStmtLoad)
        return "";

    string code = exitStmtLoad->geneCode();
    string childrenCode = generateChildren(node);
    if (!childrenCode.empty()) {
        code += " " + childrenCode;
    }
    return code + ";\n";
}

string Generator::generateExprStmtLoad(const AbstractSyntaxTree::node_ptr_t &node) {
    if (!node)
        return "";
    auto exprStmtLoad = dynamic_pointer_cast<AbstractSyntaxTree::ExprStmtLoad>(node->load());
    if (!exprStmtLoad)
        return "";

    string code = generateChildren(node);
    if (!code.empty()) {
        while (!code.empty() && (code.back() == '\n' || code.back() == ';')) {
            code.pop_back();
        }
        code += ";\n";
    }
    return code;
}

string Generator::generateStmtBlockLoad(const AbstractSyntaxTree::node_ptr_t &node) {
    if (!node)
        return "";
    auto stmtBlockLoad = dynamic_pointer_cast<AbstractSyntaxTree::StmtBlockLoad>(node->load());
    if (!stmtBlockLoad)
        return "";

    string code = "{\n";
    if (stmtBlockLoad->synced()) {
        code = "sync " + code;
    }

    string childrenCode = generateChildren(node);
    if (!childrenCode.empty()) {
        size_t pos = 0;
        while ((pos = childrenCode.find(";\n", pos)) != string::npos) {
            if (pos + 2 < childrenCode.length()) {
                childrenCode.insert(pos + 2, "    ");
            }
            pos += 3;
        }
        if (!childrenCode.empty() && childrenCode.substr(0, 4) != "    ") {
            childrenCode = "    " + childrenCode;
        }
        code += childrenCode;
    }
    code += "}";
    return code;
}

string Generator::generateDataLoad(const AbstractSyntaxTree::node_ptr_t &node) {
    if (!node)
        return "";
    auto dataLoad = dynamic_pointer_cast<AbstractSyntaxTree::DataLoad>(node->load());
    if (!dataLoad)
        return "";

    // cout << "[generateDataLoad] processing node with dataType: " << (int)dataLoad->dataType()
    //      << endl;

    string result;
    switch (dataLoad->dataType()) {
    case AbstractSyntaxTree::DataType::UnaryExpr:
        result = generateUnaryExprLoad(node);
        break;
    case AbstractSyntaxTree::DataType::BinaryExpr:
        result = generateBinaryExprLoad(node);
        break;
    case AbstractSyntaxTree::DataType::ReservedExpr:
        result = generateReservedExprLoad(node);
        break;
    case AbstractSyntaxTree::DataType::IfExpr:
        result = generateIfExprLoad(node);
        break;
    case AbstractSyntaxTree::DataType::MatchExpr:
        result = generateMatchExprLoad(node);
        break;
    case AbstractSyntaxTree::DataType::TryExpr:
        result = generateTryExprLoad(node);
        break;
    case AbstractSyntaxTree::DataType::Literal:
        result = generateLiteralLoad(node);
        break;
    case AbstractSyntaxTree::DataType::Array:
        // cout << "[generateDataLoad] calling generateListDataLoad for Array data type" << endl;
        result = generateListDataLoad(node);
        break;
    case AbstractSyntaxTree::DataType::Struct:
        result = generateDictDataLoad(node);
        break;
    case AbstractSyntaxTree::DataType::Tuple:
        result = generateTupleDataLoad(node);
        break;
    case AbstractSyntaxTree::DataType::Func:
        result = generateFuncDataLoad(node);
        break;
    case AbstractSyntaxTree::DataType::Ref:
        result = generateRefDataLoad(node);
        break;
    default:
        result = generateChildren(node);
        break;
    }

    if (dataLoad->waited() && !result.empty()) {
        result = "wait " + result;
    }

    return result;
}

string Generator::generateUnaryExprLoad(const AbstractSyntaxTree::node_ptr_t &node) {
    if (!node)
        return "";
    auto unaryExprLoad = dynamic_pointer_cast<AbstractSyntaxTree::UnaryExprLoad>(node->load());
    if (!unaryExprLoad)
        return "";

    string code = unaryExprLoad->geneCode();
    code += generateChildren(node);
    return code;
}

string Generator::generateBinaryExprLoad(const AbstractSyntaxTree::node_ptr_t &node) {
    if (!node)
        return "";
    auto binaryExprLoad = dynamic_pointer_cast<AbstractSyntaxTree::BinaryExprLoad>(node->load());
    if (!binaryExprLoad)
        return "";

    string code = "";
    if (node->size() >= 2) {
        auto leftChild = node->at(0);
        auto rightChild = node->at(1);

        if (leftChild && rightChild) {
            auto leftNode = static_pointer_cast<AbstractSyntaxTree::Node>(leftChild);
            auto rightNode = static_pointer_cast<AbstractSyntaxTree::Node>(rightChild);

            string leftCode = generateNode(leftNode);
            string rightCode = generateNode(rightNode);

            if (binaryExprLoad->op() == AbstractSyntaxTree::BinaryDataOp::Index) {
                return "(" + leftCode + ")[" + rightCode + "]";
            }

            code = leftCode + " " + binaryExprLoad->geneCode() + " " + rightCode;
        }
    } else {
        code = generateChildren(node);
    }
    return code;
}

string Generator::generateReservedExprLoad(const AbstractSyntaxTree::node_ptr_t &node) {
    if (!node)
        return "";
    auto reservedExprLoad =
        dynamic_pointer_cast<AbstractSyntaxTree::ReservedExprLoad>(node->load());
    if (!reservedExprLoad)
        return "";

    switch (reservedExprLoad->op()) {
    case AbstractSyntaxTree::ReservedDataOp::Access: {
        if (node->size() >= 2) {
            auto targetNode = node->at(0);
            auto indexNode = node->at(1);

            if (targetNode && indexNode) {
                auto targetAstNode = static_pointer_cast<AbstractSyntaxTree::Node>(targetNode);
                auto indexAstNode = static_pointer_cast<AbstractSyntaxTree::Node>(indexNode);

                string targetCode = generateNode(targetAstNode);
                string indexCode = generateNode(indexAstNode);

                return "(" + targetCode + ")[" + indexCode + "]";
            }
        }
        return "";
    }
    case AbstractSyntaxTree::ReservedDataOp::Call: {
        string code = "";
        if (node->size() >= 2) {
            auto targetNode = node->at(0);
            auto argsNode = node->at(1);

            if (targetNode && argsNode) {
                auto targetAstNode = static_pointer_cast<AbstractSyntaxTree::Node>(targetNode);
                auto argsAstNode = static_pointer_cast<AbstractSyntaxTree::Node>(argsNode);

                string targetCode = generateNode(targetAstNode);
                string normalArgs = "";

                for (size_t i = 0; i < argsAstNode->size(); ++i) {
                    auto argNode = argsAstNode->at(i);
                    if (argNode) {
                        auto argAstNode = static_pointer_cast<AbstractSyntaxTree::Node>(argNode);
                        if (!normalArgs.empty()) {
                            normalArgs += ", ";
                        }
                        normalArgs += generateNode(argAstNode);
                    }
                }

                // std::cout << "[ReservedDataOp::Call] targetCode: '" << targetCode
                //          << "', args count: " << argsAstNode->size() << std::endl;
                if (argsAstNode->size() > 0) {
                    auto firstArgNode = argsAstNode->at(0);
                    if (firstArgNode) {
                        auto firstArgAstNode =
                            static_pointer_cast<AbstractSyntaxTree::Node>(firstArgNode);
                        string firstArgCode = generateNode(firstArgAstNode);
                        // std::cout << "[ReservedDataOp::Call] first arg: '" << firstArgCode << "'"
                        //          << std::endl;
                    }
                }

                if (argsAstNode->size() >= 1 && targetCode.find("<") != string::npos &&
                    targetCode.find(">") != string::npos) {
                    auto firstArgNode = argsAstNode->at(0);
                    if (firstArgNode) {
                        auto firstArgAstNode =
                            static_pointer_cast<AbstractSyntaxTree::Node>(firstArgNode);
                        string firstArgCode = generateNode(firstArgAstNode);

                        // std::cout << "[ReservedDataOp::Call] Detected pipeline operation: "
                        //           << firstArgCode << "->" << targetCode << std::endl;
                        return firstArgCode + "->" + targetCode;
                    }
                }

                if (targetCode == "map" && argsAstNode->size() >= 2) {
                    auto arrNode = argsAstNode->at(0);
                    auto funcNode = argsAstNode->at(1);

                    if (arrNode && funcNode) {
                        auto arrAstNode = static_pointer_cast<AbstractSyntaxTree::Node>(arrNode);
                        auto funcAstNode = static_pointer_cast<AbstractSyntaxTree::Node>(funcNode);

                        string arrCode = generateNode(arrAstNode);
                        string funcCode = generateNode(funcAstNode);

                        return arrCode + ".map(" + funcCode + ")";
                    }
                }

                if (targetCode.find(".map") != string::npos && argsAstNode->size() >= 1) {
                    auto funcNode = argsAstNode->at(0);
                    if (funcNode) {
                        auto funcAstNode = static_pointer_cast<AbstractSyntaxTree::Node>(funcNode);
                        string funcCode = generateNode(funcAstNode);

                        return targetCode + "(" + funcCode + ")";
                    }
                }

                return targetCode + "(" + normalArgs + ")";
            }
        }
        return "";
    }
    case AbstractSyntaxTree::ReservedDataOp::Bind: {
        if (node->size() >= 2) {
            auto targetNode = node->at(0);
            auto argsNode = node->at(1);

            if (targetNode && argsNode) {
                auto targetAstNode = static_pointer_cast<AbstractSyntaxTree::Node>(targetNode);
                auto argsAstNode = static_pointer_cast<AbstractSyntaxTree::Node>(argsNode);

                string targetCode = generateNode(targetAstNode);
                string withParams = "";

                for (size_t i = 0; i < argsAstNode->size(); ++i) {
                    auto argNode = argsAstNode->at(i);
                    if (argNode) {
                        auto argAstNode = static_pointer_cast<AbstractSyntaxTree::Node>(argNode);
                        if (!withParams.empty()) {
                            withParams += ", ";
                        }
                        withParams += generateNode(argAstNode);
                    }
                }

                // std::cout << "[ReservedDataOp::With] targetCode: '" << targetCode
                //          << "', args count: " << argsAstNode->size() << std::endl;

                if (targetCode == "map" && argsAstNode->size() == 1) {

                    auto arrNode = argsAstNode->at(0);
                    auto arrAstNode = static_pointer_cast<AbstractSyntaxTree::Node>(arrNode);
                    string arrCode = generateNode(arrAstNode);

                    // std::cout << "[ReservedDataOp::With] Generating base method call: " <<
                    // arrCode
                    //           << ".map" << std::endl;
                    return arrCode + ".map";
                }

                if (!withParams.empty()) {
                    return targetCode + "<" + withParams + ">";
                }
            }
        }
        return "";
    }
    case AbstractSyntaxTree::ReservedDataOp::Comp: {
        if (node->size() >= 2) {
            auto leftNode = node->at(0);
            auto rightNode = node->at(1);

            if (leftNode && rightNode) {
                auto leftAstNode = static_pointer_cast<AbstractSyntaxTree::Node>(leftNode);
                auto rightAstNode = static_pointer_cast<AbstractSyntaxTree::Node>(rightNode);

                string leftCode = generateNode(leftAstNode);
                string rightCode = generateNode(rightAstNode);

                return leftCode + ".." + rightCode;
            }
        }
        return "";
    }
    case AbstractSyntaxTree::ReservedDataOp::NullThen: {
        if (node->size() >= 2) {
            auto leftNode = node->at(0);
            auto rightNode = node->at(1);

            if (leftNode && rightNode) {
                auto leftAstNode = static_pointer_cast<AbstractSyntaxTree::Node>(leftNode);
                auto rightAstNode = static_pointer_cast<AbstractSyntaxTree::Node>(rightNode);

                string leftCode = generateNode(leftAstNode);
                string rightCode = generateNode(rightAstNode);

                return leftCode + " ?? " + rightCode;
            }
        }
        return "";
    }
    default:
        return generateChildren(node);
    }
}

string Generator::generateIfExprLoad(const AbstractSyntaxTree::node_ptr_t &node) {
    if (!node)
        return "";
    auto ifExprLoad = dynamic_pointer_cast<AbstractSyntaxTree::IfExprLoad>(node->load());
    if (!ifExprLoad)
        return "";

    string code = ifExprLoad->geneCode();

    if (node->size() >= 2) {
        auto conditionNode = node->at(0);
        if (conditionNode) {
            auto conditionAstNode = static_pointer_cast<AbstractSyntaxTree::Node>(conditionNode);
            code += " " + generateNode(conditionAstNode);
        }

        code += " then";
        auto thenNode = node->at(1);
        if (thenNode) {
            auto thenAstNode = static_pointer_cast<AbstractSyntaxTree::Node>(thenNode);
            string thenCode = generateNode(thenAstNode);
            if (!thenCode.empty() && thenCode[0] != ' ' && thenCode[0] != '\n') {
                code += " " + thenCode;
            } else {
                code += thenCode;
            }
        }

        if (node->size() >= 3) {
            auto elseNode = node->at(2);
            if (elseNode) {
                auto elseAstNode = static_pointer_cast<AbstractSyntaxTree::Node>(elseNode);
                code += " else";
                string elseCode = generateNode(elseAstNode);
                if (!elseCode.empty() && elseCode[0] != ' ' && elseCode[0] != '\n') {
                    code += " " + elseCode;
                } else {
                    code += elseCode;
                }
            }
        }
    } else {
        code += " " + generateChildren(node);
    }

    return code;
}

string Generator::generateMatchExprLoad(const AbstractSyntaxTree::node_ptr_t &node) {
    if (!node)
        return "";
    auto matchExprLoad = dynamic_pointer_cast<AbstractSyntaxTree::MatchExprLoad>(node->load());
    if (!matchExprLoad)
        return "";

    string code = matchExprLoad->geneCode();
    code += " ";
    code += generateChildren(node);
    return code;
}

string Generator::generateTryExprLoad(const AbstractSyntaxTree::node_ptr_t &node) {
    if (!node)
        return "";
    auto tryExprLoad = dynamic_pointer_cast<AbstractSyntaxTree::TryExprLoad>(node->load());
    if (!tryExprLoad)
        return "";

    string code = tryExprLoad->geneCode();
    code += " ";
    code += generateChildren(node);
    return code;
}

string Generator::generateLiteralLoad(const AbstractSyntaxTree::node_ptr_t &node) {
    if (!node)
        return "";
    auto literalLoad = dynamic_pointer_cast<AbstractSyntaxTree::LiteralLoad>(node->load());
    if (!literalLoad)
        return "";

    const auto &value = literalLoad->value();
    if (value.type() == LiteralType::String) {
        return "\"" + value.toString() + "\"";
    }

    return literalLoad->geneCode();
}

string Generator::generateListDataLoad(const AbstractSyntaxTree::node_ptr_t &node) {
    if (!node)
        return "";
    auto listDataLoad = dynamic_pointer_cast<AbstractSyntaxTree::ArrayDataLoad>(node->load());
    if (!listDataLoad)
        return "";

    // cout << "[generateListDataLoad] processing list with " << node->size() << " children" <<
    // endl;
    string code = "[";

    if (node->size() == 1) {
        auto child = node->at(0);
        if (child) {
            auto childNode = static_pointer_cast<AbstractSyntaxTree::Node>(child);
            // cout << "[generateListDataLoad] single child type: " <<
            // int(childNode->load()->type())
            //      << endl;

            if (childNode->load()->type() == LoadType::Repeated ||
                childNode->load()->type() == LoadType::Optional) {
                // cout << "[generateListDataLoad] processing Repeated/Optional node with "
                //      << childNode->size() << " grandchildren" << endl;
                for (size_t i = 0; i < childNode->size(); ++i) {
                    auto grandchild = childNode->at(i);
                    if (grandchild) {
                        auto grandchildNode =
                            static_pointer_cast<AbstractSyntaxTree::Node>(grandchild);
                        string grandchildCode = generateNode(grandchildNode);
                        // cout << "[generateListDataLoad] grandchild " << i << ": " <<
                        // grandchildCode
                        //      << endl;
                        if (!grandchildCode.empty()) {
                            if (i > 0) {
                                code += ", ";
                            }
                            code += grandchildCode;
                        }
                    }
                }
            } else {
                string childCode = generateNode(childNode);
                // cout << "[generateListDataLoad] child: " << childCode << endl;
                code += childCode;
            }
        }
    } else {
        for (size_t i = 0; i < node->size(); ++i) {
            auto child = node->at(i);
            if (child) {
                auto childNode = static_pointer_cast<AbstractSyntaxTree::Node>(child);
                string childCode = generateNode(childNode);
                // cout << "[generateListDataLoad] child " << i << ": " << childCode << endl;
                if (!childCode.empty()) {
                    if (i > 0) {
                        code += ", ";
                    }
                    code += childCode;
                }
            }
        }
    }

    code += "]";
    // cout << "[generateListDataLoad] final result: " << code << endl;
    return code;
}

string Generator::generateDictDataLoad(const AbstractSyntaxTree::node_ptr_t &node) {
    if (!node)
        return "";
    auto dictDataLoad = dynamic_pointer_cast<AbstractSyntaxTree::StructDataLoad>(node->load());
    if (!dictDataLoad)
        return "";

    string code = dictDataLoad->geneCode().substr(0, 1);

    for (size_t i = 0; i < node->size(); ++i) {
        auto child = node->at(i);
        if (child) {
            auto childNode = static_pointer_cast<AbstractSyntaxTree::Node>(child);
            string childCode = generateNode(childNode);
            if (!childCode.empty()) {
                if (i > 0) {
                    code += ", ";
                }
                code += childCode;
            }
        }
    }

    code += "}";
    return code;
}

string Generator::generateTupleDataLoad(const AbstractSyntaxTree::node_ptr_t &node) {
    if (!node)
        return "";
    auto tupleDataLoad = dynamic_pointer_cast<AbstractSyntaxTree::TupleDataLoad>(node->load());
    if (!tupleDataLoad)
        return "";

    string code = tupleDataLoad->geneCode().substr(0, 1);

    for (size_t i = 0; i < node->size(); ++i) {
        auto child = node->at(i);
        if (child) {
            auto childNode = static_pointer_cast<AbstractSyntaxTree::Node>(child);
            string childCode = generateNode(childNode);
            if (!childCode.empty()) {
                if (i > 0) {
                    code += ", ";
                }
                code += childCode;
            }
        }
    }

    code += ")";
    return code;
}

string Generator::generateUnitTypeLoad(const AbstractSyntaxTree::node_ptr_t &node) {
    if (!node)
        return "";
    auto unitTypeLoad = dynamic_pointer_cast<AbstractSyntaxTree::UnitTypeLoad>(node->load());
    if (!unitTypeLoad)
        return "";

    return unitTypeLoad->geneCode();
}

string Generator::generateInferTypeLoad(const AbstractSyntaxTree::node_ptr_t &node) {
    if (!node)
        return "";
    auto inferTypeLoad = dynamic_pointer_cast<AbstractSyntaxTree::InferTypeLoad>(node->load());
    if (!inferTypeLoad)
        return "";

    return inferTypeLoad->geneCode();
}

string Generator::generateDataTypeLoad(const AbstractSyntaxTree::node_ptr_t &node) {
    if (!node)
        return "";
    auto dataTypeLoad = dynamic_pointer_cast<AbstractSyntaxTree::DataTypeLoad>(node->load());
    if (!dataTypeLoad)
        return "";

    return dataTypeLoad->geneCode();
}

string Generator::generateRefTypeLoad(const AbstractSyntaxTree::node_ptr_t &node) {
    if (!node)
        return "";
    auto refTypeLoad = dynamic_pointer_cast<AbstractSyntaxTree::RefTypeLoad>(node->load());
    if (!refTypeLoad)
        return "";

    return refTypeLoad->geneCode();
}

string Generator::generateNullableTypeLoad(const AbstractSyntaxTree::node_ptr_t &node) {
    if (!node)
        return "";
    auto nullableTypeLoad =
        dynamic_pointer_cast<AbstractSyntaxTree::NullableTypeLoad>(node->load());
    if (!nullableTypeLoad)
        return "";

    string baseType = "";
    if (node->size() > 0) {
        auto child = node->at(0);
        if (child) {
            auto childNode = static_pointer_cast<AbstractSyntaxTree::Node>(child);
            baseType = generateNode(childNode);
        }
    }

    if (!baseType.empty()) {
        return baseType + "?";
    }

    return "?";
}

string Generator::generateNamedDataLoad(const AbstractSyntaxTree::node_ptr_t &node) {
    if (!node)
        return "";
    auto namedDataLoad = dynamic_pointer_cast<AbstractSyntaxTree::NamedDataLoad>(node->load());
    if (!namedDataLoad)
        return "";

    string code = namedDataLoad->geneCode();
    code += generateChildren(node);
    return code;
}

string Generator::generateNamedTypeLoad(const AbstractSyntaxTree::node_ptr_t &node) {
    if (!node)
        return "";
    auto namedTypeLoad = dynamic_pointer_cast<AbstractSyntaxTree::NamedTypeLoad>(node->load());
    if (!namedTypeLoad)
        return "";

    string code = namedTypeLoad->geneCode();
    code += generateChildren(node);
    return code;
}

string Generator::generateNamedPairLoad(const AbstractSyntaxTree::node_ptr_t &node) {
    if (!node)
        return "";
    auto namedPairLoad = dynamic_pointer_cast<AbstractSyntaxTree::NamedPairLoad>(node->load());
    if (!namedPairLoad)
        return "";

    string code = namedPairLoad->geneCode();

    if (node->size() > 0) {
        string childrenCode = generateChildren(node);
        if (!childrenCode.empty()) {
            if (childrenCode.find(":") != string::npos) {
                code += childrenCode;
            } else {
                code += ": " + childrenCode;
            }
        }
    }

    return code;
}

string Generator::generateParamDeclLoad(const AbstractSyntaxTree::node_ptr_t &node) {
    if (!node)
        return "";
    auto paramDeclLoad = dynamic_pointer_cast<AbstractSyntaxTree::ParamDeclLoad>(node->load());
    if (!paramDeclLoad)
        return "";

    string code = paramDeclLoad->geneCode();
    code += generateChildren(node);
    return code;
}

string Generator::generateParamDataLoad(const AbstractSyntaxTree::node_ptr_t &node) {
    if (!node)
        return "";
    auto paramDataLoad = dynamic_pointer_cast<AbstractSyntaxTree::ParamDataLoad>(node->load());
    if (!paramDataLoad)
        return "";

    string code = paramDataLoad->geneCode();
    code += generateChildren(node);
    return code;
}

string Generator::generateTypeLoad(const AbstractSyntaxTree::node_ptr_t &node) {
    if (!node)
        return "";
    auto typeLoad = dynamic_pointer_cast<AbstractSyntaxTree::TypeLoad>(node->load());
    if (!typeLoad)
        return "";

    // cout << "[generateTypeLoad] processing Type node with typeType: " <<
    // int(typeLoad->typeType())
    //      << endl;
    // cout << "[generateTypeLoad] node has " << node->size() << " children" << endl;

    string baseType = typeLoad->geneCode();

    if (node->size() > 0) {
        if (typeLoad->typeType() == AbstractSyntaxTree::TypeType::Array) {
            auto arrayTypeLoad = dynamic_pointer_cast<AbstractSyntaxTree::ArrayTypeLoad>(typeLoad);
            if (arrayTypeLoad) {
                size_t dims = arrayTypeLoad->dims();
                string arraySuffix = "";
                for (size_t i = 0; i < dims; i++) {
                    arraySuffix += "[]";
                }

                if (node->size() > 0) {
                    auto child = node->at(0);
                    if (child) {
                        auto childNode = static_pointer_cast<AbstractSyntaxTree::Node>(child);
                        string elementType = generateNode(childNode);
                        return elementType + arraySuffix;
                    }
                }
                return "int" + arraySuffix;
            }
        } else if (typeLoad->typeType() == AbstractSyntaxTree::TypeType::Func) {
            // cout << "[generateTypeLoad] FuncType processing, node has " << node->size()
            //      << " children" << endl;

            string paramsCode;
            string returnTypeCode = "void";

            if (node->size() >= 2) {
                auto normParamsNode = node->at(1);
                if (normParamsNode) {
                    auto normParamsAstNode =
                        static_pointer_cast<AbstractSyntaxTree::Node>(normParamsNode);
                    if (normParamsAstNode->size() > 0) {
                        paramsCode = generateChildren(normParamsAstNode);
                        // cout << "[generateTypeLoad] normParamsCode: " << paramsCode << endl;
                    }
                }
            }

            if (node->size() >= 3) {
                auto returnTypeNode = node->at(2);
                if (returnTypeNode) {
                    auto returnTypeAstNode =
                        static_pointer_cast<AbstractSyntaxTree::Node>(returnTypeNode);
                    returnTypeCode = generateNode(returnTypeAstNode);
                    // cout << "[generateTypeLoad] returnTypeCode: " << returnTypeCode << endl;
                }
            }

            if (paramsCode.empty()) {
                return "() => " + returnTypeCode;
            } else {
                return "(" + paramsCode + ") => " + returnTypeCode;
            }
        } else {
            string childrenCode = generateChildren(node);
            // cout << "[generateTypeLoad] childrenCode: " << childrenCode << endl;

            if (!childrenCode.empty()) {
                if (childrenCode.find('[') != string::npos &&
                    childrenCode.find(']') != string::npos) {
                    return baseType + childrenCode;
                } else {
                    if (baseType == "?") {
                        return childrenCode + baseType;
                    } else {
                        return baseType + childrenCode;
                    }
                }
            }
        }
    }

    return baseType;
}

string Generator::generateFuncDataLoad(const AbstractSyntaxTree::node_ptr_t &node) {
    if (!node)
        return "";
    auto funcDataLoad = dynamic_pointer_cast<AbstractSyntaxTree::FuncDataLoad>(node->load());
    if (!funcDataLoad)
        return "";

    string code = funcDataLoad->geneCode();
    code += generateChildren(node);
    return code;
}

string Generator::generateRefDataLoad(const AbstractSyntaxTree::node_ptr_t &node) {
    if (!node)
        return "";
    auto refDataLoad = dynamic_pointer_cast<AbstractSyntaxTree::RefDataLoad>(node->load());
    if (!refDataLoad)
        return "";

    string code = refDataLoad->geneCode();
    code += generateChildren(node);
    return code;
}
