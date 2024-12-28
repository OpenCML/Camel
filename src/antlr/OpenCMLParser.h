
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
    T__44 = 45, T__45 = 46, T__46 = 47, T__47 = 48, T__48 = 49, T__49 = 50, 
    T__50 = 51, SEP = 52, AS = 53, IS = 54, LET = 55, VAR = 56, USE = 57, 
    FROM = 58, FUNC = 59, TYPE = 60, ENUM = 61, WITH = 62, WAIT = 63, RETURN = 64, 
    MODULE = 65, IMPORT = 66, EXPORT = 67, TYPEAS = 68, TYPEOF = 69, IF = 70, 
    THEN = 71, ELSE = 72, MATCH = 73, CASE = 74, FOR = 75, IN = 76, OF = 77, 
    TRY = 78, CATCH = 79, FINALLY = 80, THROW = 81, RAISE = 82, SYNC = 83, 
    MACRO = 84, INNER = 85, OUTER = 86, ATOMIC = 87, SHARED = 88, NULL_ = 89, 
    TRUE = 90, FALSE = 91, INNER_ATOM_TYPE = 92, BLANK = 93, COMMAND = 94, 
    LIN_CMT = 95, BLK_CMT = 96, MULTI_STR = 97, IDENTIFIER = 98, STRING = 99, 
    FSTRING = 100, INTEGER = 101, REAL = 102
  };

  enum {
    RuleProgram = 0, RuleDecl = 1, RuleStmt = 2, RuleStmtList = 3, RuleModuleDecl = 4, 
    RuleImportDecl = 5, RuleExportDecl = 6, RuleStmtBlock = 7, RuleLambdaExpr = 8, 
    RuleFuncDecl = 9, RuleParentIdents = 10, RuleBracedIdents = 11, RuleBracketIdents = 12, 
    RuleCarrier = 13, RuleLetDecl = 14, RuleUseDecl = 15, RuleRetStmt = 16, 
    RuleTypeDecl = 17, RuleEnumDecl = 18, RuleExprStmt = 19, RuleAnnotation = 20, 
    RuleAnnotations = 21, RuleModifiers = 22, RuleIndexValue = 23, RuleKeyTypePair = 24, 
    RuleKeyValuePair = 25, RuleKeyParamPair = 26, RuleIdentList = 27, RuleValueList = 28, 
    RuleIndexValues = 29, RulePairedValues = 30, RulePairedParams = 31, 
    RuleArgumentList = 32, RuleMemberAccess = 33, RuleParentParams = 34, 
    RuleParentArgues = 35, RuleAngledParams = 36, RuleAngledValues = 37, 
    RuleBlockExpr = 38, RuleDataExpr = 39, RulePattern = 40, RuleMatchCase = 41, 
    RuleCatchClause = 42, RuleStructExpr = 43, RuleLogicalOrExpr = 44, RuleLogicalAndExpr = 45, 
    RuleEqualityExpr = 46, RuleRelationalExpr = 47, RuleAdditiveExpr = 48, 
    RuleMultiplicativeExpr = 49, RuleNullableExpr = 50, RuleUnaryExpr = 51, 
    RuleLinkExpr = 52, RuleBindExpr = 53, RuleWithExpr = 54, RuleAnnoExpr = 55, 
    RuleDictExpr = 56, RuleListExpr = 57, RulePrimaryData = 58, RuleLiteral = 59, 
    RuleTypeExpr = 60, RuleUnionType = 61, RuleUnionUnit = 62, RuleListType = 63, 
    RuleTypeOrData = 64, RuleArgsType = 65, RulePrimaryType = 66, RuleDictExprType = 67, 
    RuleDictType = 68, RuleTupleType = 69, RuleLambdaType = 70, RuleIdentDef = 71, 
    RuleIdentRef = 72
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
  class StmtBlockContext;
  class LambdaExprContext;
  class FuncDeclContext;
  class ParentIdentsContext;
  class BracedIdentsContext;
  class BracketIdentsContext;
  class CarrierContext;
  class LetDeclContext;
  class UseDeclContext;
  class RetStmtContext;
  class TypeDeclContext;
  class EnumDeclContext;
  class ExprStmtContext;
  class AnnotationContext;
  class AnnotationsContext;
  class ModifiersContext;
  class IndexValueContext;
  class KeyTypePairContext;
  class KeyValuePairContext;
  class KeyParamPairContext;
  class IdentListContext;
  class ValueListContext;
  class IndexValuesContext;
  class PairedValuesContext;
  class PairedParamsContext;
  class ArgumentListContext;
  class MemberAccessContext;
  class ParentParamsContext;
  class ParentArguesContext;
  class AngledParamsContext;
  class AngledValuesContext;
  class BlockExprContext;
  class DataExprContext;
  class PatternContext;
  class MatchCaseContext;
  class CatchClauseContext;
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
  class AnnoExprContext;
  class DictExprContext;
  class ListExprContext;
  class PrimaryDataContext;
  class LiteralContext;
  class TypeExprContext;
  class UnionTypeContext;
  class UnionUnitContext;
  class ListTypeContext;
  class TypeOrDataContext;
  class ArgsTypeContext;
  class PrimaryTypeContext;
  class DictExprTypeContext;
  class DictTypeContext;
  class TupleTypeContext;
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
    LetDeclContext *letDecl();
    UseDeclContext *useDecl();
    FuncDeclContext *funcDecl();
    TypeDeclContext *typeDecl();
    EnumDeclContext *enumDecl();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  DeclContext* decl();

  class  StmtContext : public antlr4::RuleContextWithAltNum {
  public:
    StmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    LetDeclContext *letDecl();
    UseDeclContext *useDecl();
    FuncDeclContext *funcDecl();
    TypeDeclContext *typeDecl();
    EnumDeclContext *enumDecl();
    RetStmtContext *retStmt();
    ExprStmtContext *exprStmt();
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
    LetDeclContext *letDecl();
    TypeDeclContext *typeDecl();
    BracedIdentsContext *bracedIdents();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ExportDeclContext* exportDecl();

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

  class  ParentIdentsContext : public antlr4::RuleContextWithAltNum {
  public:
    ParentIdentsContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentListContext *identList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ParentIdentsContext* parentIdents();

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

  class  CarrierContext : public antlr4::RuleContextWithAltNum {
  public:
    CarrierContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentDefContext *identDef();
    ParentIdentsContext *parentIdents();
    BracedIdentsContext *bracedIdents();
    BracketIdentsContext *bracketIdents();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  CarrierContext* carrier();

  class  LetDeclContext : public antlr4::RuleContextWithAltNum {
  public:
    LetDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    CarrierContext *carrier();
    DataExprContext *dataExpr();
    antlr4::tree::TerminalNode *LET();
    antlr4::tree::TerminalNode *VAR();
    TypeExprContext *typeExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  LetDeclContext* letDecl();

  class  UseDeclContext : public antlr4::RuleContextWithAltNum {
  public:
    UseDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *USE();
    IdentRefContext *identRef();
    IdentDefContext *identDef();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  UseDeclContext* useDecl();

  class  RetStmtContext : public antlr4::RuleContextWithAltNum {
  public:
    RetStmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    DataExprContext *dataExpr();
    antlr4::tree::TerminalNode *RETURN();
    antlr4::tree::TerminalNode *RAISE();
    antlr4::tree::TerminalNode *THROW();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  RetStmtContext* retStmt();

  class  TypeDeclContext : public antlr4::RuleContextWithAltNum {
  public:
    TypeDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *TYPE();
    IdentDefContext *identDef();
    TypeExprContext *typeExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  TypeDeclContext* typeDecl();

  class  EnumDeclContext : public antlr4::RuleContextWithAltNum {
  public:
    EnumDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *ENUM();
    IdentDefContext *identDef();
    PairedValuesContext *pairedValues();
    antlr4::tree::TerminalNode *OF();
    TypeExprContext *typeExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  EnumDeclContext* enumDecl();

  class  ExprStmtContext : public antlr4::RuleContextWithAltNum {
  public:
    ExprStmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    DataExprContext *dataExpr();
    AnnotationsContext *annotations();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ExprStmtContext* exprStmt();

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
    std::vector<IdentDefContext *> identDef();
    IdentDefContext* identDef(size_t i);
    TypeExprContext *typeExpr();
    antlr4::tree::TerminalNode *TYPEAS();
    antlr4::tree::TerminalNode *VAR();
    AnnotationContext *annotation();
    DataExprContext *dataExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  KeyParamPairContext* keyParamPair();

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

  class  PatternContext : public antlr4::RuleContextWithAltNum {
  public:
    PatternContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentRefContext *identRef();
    LiteralContext *literal();
    ValueListContext *valueList();
    IdentListContext *identList();
    PairedValuesContext *pairedValues();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  PatternContext* pattern();

  class  MatchCaseContext : public antlr4::RuleContextWithAltNum {
  public:
    MatchCaseContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *CASE();
    std::vector<PatternContext *> pattern();
    PatternContext* pattern(size_t i);
    BlockExprContext *blockExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  MatchCaseContext* matchCase();

  class  CatchClauseContext : public antlr4::RuleContextWithAltNum {
  public:
    CatchClauseContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *CATCH();
    IdentDefContext *identDef();
    TypeExprContext *typeExpr();
    StmtBlockContext *stmtBlock();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  CatchClauseContext* catchClause();

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
    std::vector<MatchCaseContext *> matchCase();
    MatchCaseContext* matchCase(size_t i);
    antlr4::tree::TerminalNode *TRY();
    std::vector<StmtBlockContext *> stmtBlock();
    StmtBlockContext* stmtBlock(size_t i);
    std::vector<CatchClauseContext *> catchClause();
    CatchClauseContext* catchClause(size_t i);
    antlr4::tree::TerminalNode *FINALLY();


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
    std::vector<AnnoExprContext *> annoExpr();
    AnnoExprContext* annoExpr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  WithExprContext* withExpr();

  class  AnnoExprContext : public antlr4::RuleContextWithAltNum {
  public:
    AnnoExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
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

  AnnoExprContext* annoExpr();

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
    ValueListContext *valueList();
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

  class  TypeExprContext : public antlr4::RuleContextWithAltNum {
  public:
    TypeExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<UnionTypeContext *> unionType();
    UnionTypeContext* unionType(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  TypeExprContext* typeExpr();

  class  UnionTypeContext : public antlr4::RuleContextWithAltNum {
  public:
    UnionTypeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<UnionUnitContext *> unionUnit();
    UnionUnitContext* unionUnit(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  UnionTypeContext* unionType();

  class  UnionUnitContext : public antlr4::RuleContextWithAltNum {
  public:
    UnionUnitContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ListTypeContext *listType();
    IdentDefContext *identDef();
    antlr4::tree::TerminalNode *OF();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  UnionUnitContext* unionUnit();

  class  ListTypeContext : public antlr4::RuleContextWithAltNum {
  public:
    ListTypeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ArgsTypeContext *argsType();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ListTypeContext* listType();

  class  TypeOrDataContext : public antlr4::RuleContextWithAltNum {
  public:
    TypeOrDataContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    TypeExprContext *typeExpr();
    PrimaryDataContext *primaryData();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  TypeOrDataContext* typeOrData();

  class  ArgsTypeContext : public antlr4::RuleContextWithAltNum {
  public:
    ArgsTypeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    PrimaryTypeContext *primaryType();
    std::vector<TypeOrDataContext *> typeOrData();
    TypeOrDataContext* typeOrData(size_t i);


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
    TupleTypeContext *tupleType();
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
    std::vector<KeyTypePairContext *> keyTypePair();
    KeyTypePairContext* keyTypePair(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  DictTypeContext* dictType();

  class  TupleTypeContext : public antlr4::RuleContextWithAltNum {
  public:
    TupleTypeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<TypeExprContext *> typeExpr();
    TypeExprContext* typeExpr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  TupleTypeContext* tupleType();

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

  bool annoExprSempred(AnnoExprContext *_localctx, size_t predicateIndex);

  // By default the static state used to implement the parser is lazily initialized during the first
  // call to the constructor. You can call this function if you wish to initialize the static state
  // ahead of time.
  static void initialize();

private:
};

