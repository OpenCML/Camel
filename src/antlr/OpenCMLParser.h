
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
 * Supported by: National Key Research and Development Program of China
 */


// Generated from OpenCML.g4 by ANTLR 4.13.1

#pragma once


#include "antlr4-runtime.h"




class  OpenCMLParser : public antlr4::Parser {
public:
  enum {
    T__0 = 1, T__1 = 2, T__2 = 3, T__3 = 4, T__4 = 5, T__5 = 6, T__6 = 7, 
    T__7 = 8, T__8 = 9, T__9 = 10, T__10 = 11, T__11 = 12, T__12 = 13, T__13 = 14, 
    T__14 = 15, T__15 = 16, T__16 = 17, T__17 = 18, T__18 = 19, T__19 = 20, 
    T__20 = 21, T__21 = 22, T__22 = 23, T__23 = 24, T__24 = 25, T__25 = 26, 
    T__26 = 27, T__27 = 28, T__28 = 29, T__29 = 30, T__30 = 31, T__31 = 32, 
    T__32 = 33, T__33 = 34, T__34 = 35, T__35 = 36, T__36 = 37, T__37 = 38, 
    T__38 = 39, T__39 = 40, T__40 = 41, T__41 = 42, T__42 = 43, T__43 = 44, 
    T__44 = 45, T__45 = 46, T__46 = 47, T__47 = 48, T__48 = 49, SEP = 50, 
    AS = 51, IS = 52, LET = 53, VAR = 54, USE = 55, FROM = 56, FUNC = 57, 
    TYPE = 58, ENUM = 59, WITH = 60, WAIT = 61, RETURN = 62, MODULE = 63, 
    IMPORT = 64, EXPORT = 65, TYPEAS = 66, TYPEOF = 67, IF = 68, THEN = 69, 
    ELSE = 70, MATCH = 71, CASE = 72, FOR = 73, IN = 74, SYNC = 75, MACRO = 76, 
    INNER = 77, OUTER = 78, ATOMIC = 79, SHARED = 80, NULL_ = 81, TRUE = 82, 
    FALSE = 83, INNER_ATOM_TYPE = 84, BLANK = 85, COMMAND = 86, LIN_CMT = 87, 
    BLK_CMT = 88, MULTI_STR = 89, IDENTIFIER = 90, STRING = 91, FSTRING = 92, 
    INTEGER = 93, REAL = 94
  };

  enum {
    RuleProgram = 0, RuleDecl = 1, RuleStmt = 2, RuleStmtList = 3, RuleModuleDecl = 4, 
    RuleImportDecl = 5, RuleExportDecl = 6, RuleFuncDecl = 7, RuleLetStmt = 8, 
    RuleUseStmt = 9, RuleTypeStmt = 10, RuleEnumStmt = 11, RuleExprStmt = 12, 
    RuleRetStmt = 13, RuleStmtBlock = 14, RuleLambdaExpr = 15, RuleCarrier = 16, 
    RuleAnnotation = 17, RuleAnnotations = 18, RuleModifiers = 19, RuleIndexValue = 20, 
    RuleKeyTypePair = 21, RuleKeyValuePair = 22, RuleKeyParamPair = 23, 
    RuleTypeList = 24, RuleIdentList = 25, RuleValueList = 26, RuleIndexValues = 27, 
    RulePairedTypes = 28, RulePairedValues = 29, RulePairedParams = 30, 
    RuleArgumentList = 31, RuleBracedIdents = 32, RuleBracketIdents = 33, 
    RuleMemberAccess = 34, RuleParentParams = 35, RuleParentArgues = 36, 
    RuleParentValues = 37, RuleAngledParams = 38, RuleAngledValues = 39, 
    RuleBlockExpr = 40, RuleDataExpr = 41, RuleStructExpr = 42, RuleLogicalOrExpr = 43, 
    RuleLogicalAndExpr = 44, RuleEqualityExpr = 45, RuleRelationalExpr = 46, 
    RuleAdditiveExpr = 47, RuleMultiplicativeExpr = 48, RuleNullableExpr = 49, 
    RuleUnaryExpr = 50, RuleLinkExpr = 51, RuleBindExpr = 52, RuleWithExpr = 53, 
    RuleAnnotatedExpr = 54, RuleDictExpr = 55, RuleListExpr = 56, RulePrimaryData = 57, 
    RuleLiteral = 58, RuleTypeName = 59, RuleTypeExpr = 60, RuleUnionType = 61, 
    RuleListType = 62, RuleArgsType = 63, RulePrimaryType = 64, RuleDictExprType = 65, 
    RuleDictType = 66, RuleLambdaType = 67, RuleIdentDef = 68, RuleIdentRef = 69
  };

  explicit OpenCMLParser(antlr4::TokenStream *input);

  OpenCMLParser(antlr4::TokenStream *input, const antlr4::atn::ParserATNSimulatorOptions &options);

  ~OpenCMLParser() override;

  std::string getGrammarFileName() const override;

  const antlr4::atn::ATN& getATN() const override;

  const std::vector<std::string>& getRuleNames() const override;

  const antlr4::dfa::Vocabulary& getVocabulary() const override;

  antlr4::atn::SerializedATNView getSerializedATN() const override;


  bool isAdjacent() {
      const antlr4::Token *last = _input->LT(-1);
      const antlr4::Token *curr = _input->LT(1);
      if (last == nullptr || curr == nullptr)
          return false;
      if (last->getStopIndex() + 1 != curr->getStartIndex())
          return false;
      return true;
  }


  class ProgramContext;
  class DeclContext;
  class StmtContext;
  class StmtListContext;
  class ModuleDeclContext;
  class ImportDeclContext;
  class ExportDeclContext;
  class FuncDeclContext;
  class LetStmtContext;
  class UseStmtContext;
  class TypeStmtContext;
  class EnumStmtContext;
  class ExprStmtContext;
  class RetStmtContext;
  class StmtBlockContext;
  class LambdaExprContext;
  class CarrierContext;
  class AnnotationContext;
  class AnnotationsContext;
  class ModifiersContext;
  class IndexValueContext;
  class KeyTypePairContext;
  class KeyValuePairContext;
  class KeyParamPairContext;
  class TypeListContext;
  class IdentListContext;
  class ValueListContext;
  class IndexValuesContext;
  class PairedTypesContext;
  class PairedValuesContext;
  class PairedParamsContext;
  class ArgumentListContext;
  class BracedIdentsContext;
  class BracketIdentsContext;
  class MemberAccessContext;
  class ParentParamsContext;
  class ParentArguesContext;
  class ParentValuesContext;
  class AngledParamsContext;
  class AngledValuesContext;
  class BlockExprContext;
  class DataExprContext;
  class StructExprContext;
  class LogicalOrExprContext;
  class LogicalAndExprContext;
  class EqualityExprContext;
  class RelationalExprContext;
  class AdditiveExprContext;
  class MultiplicativeExprContext;
  class NullableExprContext;
  class UnaryExprContext;
  class LinkExprContext;
  class BindExprContext;
  class WithExprContext;
  class AnnotatedExprContext;
  class DictExprContext;
  class ListExprContext;
  class PrimaryDataContext;
  class LiteralContext;
  class TypeNameContext;
  class TypeExprContext;
  class UnionTypeContext;
  class ListTypeContext;
  class ArgsTypeContext;
  class PrimaryTypeContext;
  class DictExprTypeContext;
  class DictTypeContext;
  class LambdaTypeContext;
  class IdentDefContext;
  class IdentRefContext; 

  class  ProgramContext : public antlr4::RuleContextWithAltNum {
  public:
    ProgramContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *EOF();
    std::vector<antlr4::tree::TerminalNode *> SEP();
    antlr4::tree::TerminalNode* SEP(size_t i);
    std::vector<DeclContext *> decl();
    DeclContext* decl(size_t i);
    std::vector<StmtContext *> stmt();
    StmtContext* stmt(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ProgramContext* program();

  class  DeclContext : public antlr4::RuleContextWithAltNum {
  public:
    DeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ModuleDeclContext *moduleDecl();
    ImportDeclContext *importDecl();
    ExportDeclContext *exportDecl();
    FuncDeclContext *funcDecl();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  DeclContext* decl();

  class  StmtContext : public antlr4::RuleContextWithAltNum {
  public:
    StmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    LetStmtContext *letStmt();
    UseStmtContext *useStmt();
    TypeStmtContext *typeStmt();
    EnumStmtContext *enumStmt();
    ExprStmtContext *exprStmt();
    RetStmtContext *retStmt();
    StmtBlockContext *stmtBlock();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  StmtContext* stmt();

  class  StmtListContext : public antlr4::RuleContextWithAltNum {
  public:
    StmtListContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<StmtContext *> stmt();
    StmtContext* stmt(size_t i);
    std::vector<antlr4::tree::TerminalNode *> SEP();
    antlr4::tree::TerminalNode* SEP(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  StmtListContext* stmtList();

  class  ModuleDeclContext : public antlr4::RuleContextWithAltNum {
  public:
    ModuleDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *MODULE();
    IdentDefContext *identDef();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ModuleDeclContext* moduleDecl();

  class  ImportDeclContext : public antlr4::RuleContextWithAltNum {
  public:
    ImportDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *IMPORT();
    antlr4::tree::TerminalNode *STRING();
    antlr4::tree::TerminalNode *FROM();
    IdentDefContext *identDef();
    BracedIdentsContext *bracedIdents();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ImportDeclContext* importDecl();

  class  ExportDeclContext : public antlr4::RuleContextWithAltNum {
  public:
    ExportDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *EXPORT();
    LetStmtContext *letStmt();
    TypeStmtContext *typeStmt();
    BracedIdentsContext *bracedIdents();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ExportDeclContext* exportDecl();

  class  FuncDeclContext : public antlr4::RuleContextWithAltNum {
  public:
    FuncDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *FUNC();
    IdentDefContext *identDef();
    ParentParamsContext *parentParams();
    StmtBlockContext *stmtBlock();
    AnnotationsContext *annotations();
    antlr4::tree::TerminalNode *WITH();
    AngledParamsContext *angledParams();
    antlr4::tree::TerminalNode *EXPORT();
    ModifiersContext *modifiers();
    TypeExprContext *typeExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  FuncDeclContext* funcDecl();

  class  LetStmtContext : public antlr4::RuleContextWithAltNum {
  public:
    LetStmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    CarrierContext *carrier();
    DataExprContext *dataExpr();
    antlr4::tree::TerminalNode *LET();
    antlr4::tree::TerminalNode *VAR();
    TypeExprContext *typeExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  LetStmtContext* letStmt();

  class  UseStmtContext : public antlr4::RuleContextWithAltNum {
  public:
    UseStmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *USE();
    IdentRefContext *identRef();
    IdentDefContext *identDef();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  UseStmtContext* useStmt();

  class  TypeStmtContext : public antlr4::RuleContextWithAltNum {
  public:
    TypeStmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *TYPE();
    IdentDefContext *identDef();
    TypeExprContext *typeExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  TypeStmtContext* typeStmt();

  class  EnumStmtContext : public antlr4::RuleContextWithAltNum {
  public:
    EnumStmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *ENUM();
    IdentDefContext *identDef();
    BracedIdentsContext *bracedIdents();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  EnumStmtContext* enumStmt();

  class  ExprStmtContext : public antlr4::RuleContextWithAltNum {
  public:
    ExprStmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    DataExprContext *dataExpr();
    AnnotationsContext *annotations();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ExprStmtContext* exprStmt();

  class  RetStmtContext : public antlr4::RuleContextWithAltNum {
  public:
    RetStmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *RETURN();
    DataExprContext *dataExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  RetStmtContext* retStmt();

  class  StmtBlockContext : public antlr4::RuleContextWithAltNum {
  public:
    StmtBlockContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *SYNC();
    StmtListContext *stmtList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  StmtBlockContext* stmtBlock();

  class  LambdaExprContext : public antlr4::RuleContextWithAltNum {
  public:
    LambdaExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ParentParamsContext *parentParams();
    BlockExprContext *blockExpr();
    ModifiersContext *modifiers();
    AngledParamsContext *angledParams();
    TypeExprContext *typeExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  LambdaExprContext* lambdaExpr();

  class  CarrierContext : public antlr4::RuleContextWithAltNum {
  public:
    CarrierContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentDefContext *identDef();
    BracedIdentsContext *bracedIdents();
    BracketIdentsContext *bracketIdents();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  CarrierContext* carrier();

  class  AnnotationContext : public antlr4::RuleContextWithAltNum {
  public:
    AnnotationContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    PrimaryDataContext *primaryData();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  AnnotationContext* annotation();

  class  AnnotationsContext : public antlr4::RuleContextWithAltNum {
  public:
    AnnotationsContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<AnnotationContext *> annotation();
    AnnotationContext* annotation(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  AnnotationsContext* annotations();

  class  ModifiersContext : public antlr4::RuleContextWithAltNum {
  public:
    ModifiersContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<antlr4::tree::TerminalNode *> INNER();
    antlr4::tree::TerminalNode* INNER(size_t i);
    std::vector<antlr4::tree::TerminalNode *> OUTER();
    antlr4::tree::TerminalNode* OUTER(size_t i);
    std::vector<antlr4::tree::TerminalNode *> ATOMIC();
    antlr4::tree::TerminalNode* ATOMIC(size_t i);
    std::vector<antlr4::tree::TerminalNode *> SHARED();
    antlr4::tree::TerminalNode* SHARED(size_t i);
    std::vector<antlr4::tree::TerminalNode *> SYNC();
    antlr4::tree::TerminalNode* SYNC(size_t i);
    std::vector<antlr4::tree::TerminalNode *> MACRO();
    antlr4::tree::TerminalNode* MACRO(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ModifiersContext* modifiers();

  class  IndexValueContext : public antlr4::RuleContextWithAltNum {
  public:
    IndexValueContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    DataExprContext *dataExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  IndexValueContext* indexValue();

  class  KeyTypePairContext : public antlr4::RuleContextWithAltNum {
  public:
    KeyTypePairContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentDefContext *identDef();
    TypeExprContext *typeExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  KeyTypePairContext* keyTypePair();

  class  KeyValuePairContext : public antlr4::RuleContextWithAltNum {
  public:
    KeyValuePairContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentDefContext *identDef();
    DataExprContext *dataExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  KeyValuePairContext* keyValuePair();

  class  KeyParamPairContext : public antlr4::RuleContextWithAltNum {
  public:
    KeyParamPairContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentDefContext *identDef();
    TypeExprContext *typeExpr();
    TypeNameContext *typeName();
    antlr4::tree::TerminalNode *VAR();
    AnnotationContext *annotation();
    DataExprContext *dataExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  KeyParamPairContext* keyParamPair();

  class  TypeListContext : public antlr4::RuleContextWithAltNum {
  public:
    TypeListContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<TypeExprContext *> typeExpr();
    TypeExprContext* typeExpr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  TypeListContext* typeList();

  class  IdentListContext : public antlr4::RuleContextWithAltNum {
  public:
    IdentListContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<IdentDefContext *> identDef();
    IdentDefContext* identDef(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  IdentListContext* identList();

  class  ValueListContext : public antlr4::RuleContextWithAltNum {
  public:
    ValueListContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<DataExprContext *> dataExpr();
    DataExprContext* dataExpr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ValueListContext* valueList();

  class  IndexValuesContext : public antlr4::RuleContextWithAltNum {
  public:
    IndexValuesContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<IndexValueContext *> indexValue();
    IndexValueContext* indexValue(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  IndexValuesContext* indexValues();

  class  PairedTypesContext : public antlr4::RuleContextWithAltNum {
  public:
    PairedTypesContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<KeyTypePairContext *> keyTypePair();
    KeyTypePairContext* keyTypePair(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  PairedTypesContext* pairedTypes();

  class  PairedValuesContext : public antlr4::RuleContextWithAltNum {
  public:
    PairedValuesContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<KeyValuePairContext *> keyValuePair();
    KeyValuePairContext* keyValuePair(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  PairedValuesContext* pairedValues();

  class  PairedParamsContext : public antlr4::RuleContextWithAltNum {
  public:
    PairedParamsContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<KeyParamPairContext *> keyParamPair();
    KeyParamPairContext* keyParamPair(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  PairedParamsContext* pairedParams();

  class  ArgumentListContext : public antlr4::RuleContextWithAltNum {
  public:
    ArgumentListContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IndexValuesContext *indexValues();
    PairedValuesContext *pairedValues();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ArgumentListContext* argumentList();

  class  BracedIdentsContext : public antlr4::RuleContextWithAltNum {
  public:
    BracedIdentsContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentListContext *identList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  BracedIdentsContext* bracedIdents();

  class  BracketIdentsContext : public antlr4::RuleContextWithAltNum {
  public:
    BracketIdentsContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentListContext *identList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  BracketIdentsContext* bracketIdents();

  class  MemberAccessContext : public antlr4::RuleContextWithAltNum {
  public:
    MemberAccessContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<DataExprContext *> dataExpr();
    DataExprContext* dataExpr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  MemberAccessContext* memberAccess();

  class  ParentParamsContext : public antlr4::RuleContextWithAltNum {
  public:
    ParentParamsContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    PairedParamsContext *pairedParams();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ParentParamsContext* parentParams();

  class  ParentArguesContext : public antlr4::RuleContextWithAltNum {
  public:
    ParentArguesContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ArgumentListContext *argumentList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ParentArguesContext* parentArgues();

  class  ParentValuesContext : public antlr4::RuleContextWithAltNum {
  public:
    ParentValuesContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ValueListContext *valueList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ParentValuesContext* parentValues();

  class  AngledParamsContext : public antlr4::RuleContextWithAltNum {
  public:
    AngledParamsContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    PairedParamsContext *pairedParams();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  AngledParamsContext* angledParams();

  class  AngledValuesContext : public antlr4::RuleContextWithAltNum {
  public:
    AngledValuesContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ArgumentListContext *argumentList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  AngledValuesContext* angledValues();

  class  BlockExprContext : public antlr4::RuleContextWithAltNum {
  public:
    BlockExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    StmtBlockContext *stmtBlock();
    DataExprContext *dataExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  BlockExprContext* blockExpr();

  class  DataExprContext : public antlr4::RuleContextWithAltNum {
  public:
    DataExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<StructExprContext *> structExpr();
    StructExprContext* structExpr(size_t i);
    antlr4::tree::TerminalNode *WAIT();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  DataExprContext* dataExpr();

  class  StructExprContext : public antlr4::RuleContextWithAltNum {
  public:
    StructExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    LogicalOrExprContext *logicalOrExpr();
    antlr4::tree::TerminalNode *IF();
    antlr4::tree::TerminalNode *THEN();
    std::vector<BlockExprContext *> blockExpr();
    BlockExprContext* blockExpr(size_t i);
    antlr4::tree::TerminalNode *ELSE();
    antlr4::tree::TerminalNode *MATCH();
    IdentRefContext *identRef();
    std::vector<antlr4::tree::TerminalNode *> CASE();
    antlr4::tree::TerminalNode* CASE(size_t i);
    std::vector<DataExprContext *> dataExpr();
    DataExprContext* dataExpr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  StructExprContext* structExpr();

  class  LogicalOrExprContext : public antlr4::RuleContextWithAltNum {
  public:
    LogicalOrExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<LogicalAndExprContext *> logicalAndExpr();
    LogicalAndExprContext* logicalAndExpr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  LogicalOrExprContext* logicalOrExpr();

  class  LogicalAndExprContext : public antlr4::RuleContextWithAltNum {
  public:
    LogicalAndExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<EqualityExprContext *> equalityExpr();
    EqualityExprContext* equalityExpr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  LogicalAndExprContext* logicalAndExpr();

  class  EqualityExprContext : public antlr4::RuleContextWithAltNum {
  public:
    EqualityExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<RelationalExprContext *> relationalExpr();
    RelationalExprContext* relationalExpr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  EqualityExprContext* equalityExpr();

  class  RelationalExprContext : public antlr4::RuleContextWithAltNum {
  public:
    RelationalExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<AdditiveExprContext *> additiveExpr();
    AdditiveExprContext* additiveExpr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  RelationalExprContext* relationalExpr();

  class  AdditiveExprContext : public antlr4::RuleContextWithAltNum {
  public:
    AdditiveExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<MultiplicativeExprContext *> multiplicativeExpr();
    MultiplicativeExprContext* multiplicativeExpr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  AdditiveExprContext* additiveExpr();

  class  MultiplicativeExprContext : public antlr4::RuleContextWithAltNum {
  public:
    MultiplicativeExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<NullableExprContext *> nullableExpr();
    NullableExprContext* nullableExpr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  MultiplicativeExprContext* multiplicativeExpr();

  class  NullableExprContext : public antlr4::RuleContextWithAltNum {
  public:
    NullableExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    UnaryExprContext *unaryExpr();
    DataExprContext *dataExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  NullableExprContext* nullableExpr();

  class  UnaryExprContext : public antlr4::RuleContextWithAltNum {
  public:
    UnaryExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    LinkExprContext *linkExpr();
    TypeExprContext *typeExpr();
    antlr4::tree::TerminalNode *AS();
    antlr4::tree::TerminalNode *IS();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  UnaryExprContext* unaryExpr();

  class  LinkExprContext : public antlr4::RuleContextWithAltNum {
  public:
    LinkExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<BindExprContext *> bindExpr();
    BindExprContext* bindExpr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  LinkExprContext* linkExpr();

  class  BindExprContext : public antlr4::RuleContextWithAltNum {
  public:
    BindExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<WithExprContext *> withExpr();
    WithExprContext* withExpr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  BindExprContext* bindExpr();

  class  WithExprContext : public antlr4::RuleContextWithAltNum {
  public:
    WithExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<AnnotatedExprContext *> annotatedExpr();
    AnnotatedExprContext* annotatedExpr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  WithExprContext* withExpr();

  class  AnnotatedExprContext : public antlr4::RuleContextWithAltNum {
  public:
    AnnotatedExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    PrimaryDataContext *primaryData();
    std::vector<AnnotationContext *> annotation();
    AnnotationContext* annotation(size_t i);
    std::vector<MemberAccessContext *> memberAccess();
    MemberAccessContext* memberAccess(size_t i);
    std::vector<ParentArguesContext *> parentArgues();
    ParentArguesContext* parentArgues(size_t i);
    std::vector<AngledValuesContext *> angledValues();
    AngledValuesContext* angledValues(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  AnnotatedExprContext* annotatedExpr();

  class  DictExprContext : public antlr4::RuleContextWithAltNum {
  public:
    DictExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    PairedValuesContext *pairedValues();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  DictExprContext* dictExpr();

  class  ListExprContext : public antlr4::RuleContextWithAltNum {
  public:
    ListExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<DataExprContext *> dataExpr();
    DataExprContext* dataExpr(size_t i);
    antlr4::tree::TerminalNode *FOR();
    IdentRefContext *identRef();
    antlr4::tree::TerminalNode *IN();
    IndexValuesContext *indexValues();
    antlr4::tree::TerminalNode *IF();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ListExprContext* listExpr();

  class  PrimaryDataContext : public antlr4::RuleContextWithAltNum {
  public:
    PrimaryDataContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentRefContext *identRef();
    LiteralContext *literal();
    ListExprContext *listExpr();
    DictExprContext *dictExpr();
    DataExprContext *dataExpr();
    ParentValuesContext *parentValues();
    LambdaExprContext *lambdaExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  PrimaryDataContext* primaryData();

  class  LiteralContext : public antlr4::RuleContextWithAltNum {
  public:
    LiteralContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *INTEGER();
    antlr4::tree::TerminalNode *REAL();
    antlr4::tree::TerminalNode *STRING();
    antlr4::tree::TerminalNode *MULTI_STR();
    antlr4::tree::TerminalNode *FSTRING();
    antlr4::tree::TerminalNode *TRUE();
    antlr4::tree::TerminalNode *FALSE();
    antlr4::tree::TerminalNode *NULL_();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  LiteralContext* literal();

  class  TypeNameContext : public antlr4::RuleContextWithAltNum {
  public:
    TypeNameContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *TYPEAS();
    IdentDefContext *identDef();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  TypeNameContext* typeName();

  class  TypeExprContext : public antlr4::RuleContextWithAltNum {
  public:
    TypeExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    UnionTypeContext *unionType();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  TypeExprContext* typeExpr();

  class  UnionTypeContext : public antlr4::RuleContextWithAltNum {
  public:
    UnionTypeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<ListTypeContext *> listType();
    ListTypeContext* listType(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  UnionTypeContext* unionType();

  class  ListTypeContext : public antlr4::RuleContextWithAltNum {
  public:
    ListTypeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ArgsTypeContext *argsType();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ListTypeContext* listType();

  class  ArgsTypeContext : public antlr4::RuleContextWithAltNum {
  public:
    ArgsTypeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    PrimaryTypeContext *primaryType();
    TypeListContext *typeList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ArgsTypeContext* argsType();

  class  PrimaryTypeContext : public antlr4::RuleContextWithAltNum {
  public:
    PrimaryTypeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *INNER_ATOM_TYPE();
    DictExprTypeContext *dictExprType();
    IdentRefContext *identRef();
    TypeExprContext *typeExpr();
    LambdaTypeContext *lambdaType();
    antlr4::tree::TerminalNode *TYPEOF();
    DataExprContext *dataExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  PrimaryTypeContext* primaryType();

  class  DictExprTypeContext : public antlr4::RuleContextWithAltNum {
  public:
    DictExprTypeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<DictTypeContext *> dictType();
    DictTypeContext* dictType(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  DictExprTypeContext* dictExprType();

  class  DictTypeContext : public antlr4::RuleContextWithAltNum {
  public:
    DictTypeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    PairedTypesContext *pairedTypes();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  DictTypeContext* dictType();

  class  LambdaTypeContext : public antlr4::RuleContextWithAltNum {
  public:
    LambdaTypeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ParentParamsContext *parentParams();
    TypeExprContext *typeExpr();
    ModifiersContext *modifiers();
    AngledParamsContext *angledParams();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  LambdaTypeContext* lambdaType();

  class  IdentDefContext : public antlr4::RuleContextWithAltNum {
  public:
    IdentDefContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *IDENTIFIER();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  IdentDefContext* identDef();

  class  IdentRefContext : public antlr4::RuleContextWithAltNum {
  public:
    IdentRefContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<antlr4::tree::TerminalNode *> IDENTIFIER();
    antlr4::tree::TerminalNode* IDENTIFIER(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  IdentRefContext* identRef();


  bool sempred(antlr4::RuleContext *_localctx, size_t ruleIndex, size_t predicateIndex) override;

  bool annotatedExprSempred(AnnotatedExprContext *_localctx, size_t predicateIndex);

  // By default the static state used to implement the parser is lazily initialized during the first
  // call to the constructor. You can call this function if you wish to initialize the static state
  // ahead of time.
  static void initialize();

private:
};

