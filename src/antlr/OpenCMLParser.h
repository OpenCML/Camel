
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
    T__44 = 45, T__45 = 46, T__46 = 47, SEP = 48, AS = 49, IS = 50, LET = 51, 
    VAR = 52, USE = 53, FROM = 54, FUNC = 55, TYPE = 56, WITH = 57, WAIT = 58, 
    RETURN = 59, INNER = 60, OUTER = 61, ATOMIC = 62, STATIC = 63, NULL_ = 64, 
    TRUE = 65, FALSE = 66, INTEGER_TYPE = 67, INTEGER32_TYPE = 68, INTEGER64_TYPE = 69, 
    REAL_TYPE = 70, FLOAT_TYPE = 71, DOUBLE_TYPE = 72, NUMBER_TYPE = 73, 
    STRING_TYPE = 74, BOOL_TYPE = 75, CHAR_TYPE = 76, SET_TYPE = 77, MAP_TYPE = 78, 
    LIST_TYPE = 79, DICT_TYPE = 80, ARRAY_TYPE = 81, TUPLE_TYPE = 82, UNION_TYPE = 83, 
    VECTOR_TYPE = 84, TENSOR_TYPE = 85, ANY_TYPE = 86, VOID_TYPE = 87, FUNCTOR_TYPE = 88, 
    BLANK = 89, LIN_CMT_HASH = 90, LIN_CMT_SLASH = 91, BLK_CMT = 92, MULTI_STR = 93, 
    IDENTIFIER = 94, UNIT = 95, STRING = 96, FSTRING = 97, INTEGER = 98, 
    REAL = 99
  };

  enum {
    RuleProgram = 0, RuleStmtList = 1, RuleStmt = 2, RuleLetStmt = 3, RuleUseStmt = 4, 
    RuleTypeStmt = 5, RuleExprStmt = 6, RuleWaitStmt = 7, RuleWithDef = 8, 
    RuleFuncDef = 9, RuleRetStmt = 10, RuleLambdaExpr = 11, RuleCarrier = 12, 
    RuleAnnotation = 13, RuleAnnotations = 14, RuleModifiers = 15, RuleKeyTypePair = 16, 
    RuleKeyValuePair = 17, RuleKeyParamPair = 18, RuleIndexKTPair = 19, 
    RuleIndexKVPair = 20, RuleTypeList = 21, RuleIdentList = 22, RuleValueList = 23, 
    RulePairedTypes = 24, RulePairedValues = 25, RulePairedParams = 26, 
    RuleIndexKVPairs = 27, RuleArgumentList = 28, RuleBracedPairedValues = 29, 
    RuleBracedIdents = 30, RuleBracedStmts = 31, RuleBracedHomoValues = 32, 
    RuleBracedIndexKVPairs = 33, RuleBracketIdents = 34, RuleBracketHomoValues = 35, 
    RuleBracketHeteValues = 36, RuleParentParams = 37, RuleParentValues = 38, 
    RuleParentHomoValues = 39, RuleParentHeteValues = 40, RuleAngledParams = 41, 
    RuleAngledValues = 42, RulePrimEntity = 43, RuleMemberAccess = 44, RuleEntityLink = 45, 
    RuleEntityUnit = 46, RuleEntityWith = 47, RuleEntityExpr = 48, RuleRelaExpr = 49, 
    RuleAddExpr = 50, RuleMultiExpr = 51, RuleUnaryExpr = 52, RulePrimExpr = 53, 
    RuleLiteral = 54, RuleTypeExpr = 55, RuleUnaryType = 56, RuleAtomType = 57, 
    RuleLambdaType = 58, RulePrimType = 59, RuleStructType = 60, RuleSpecialType = 61, 
    RuleIdentRef = 62
  };

  explicit OpenCMLParser(antlr4::TokenStream *input);

  OpenCMLParser(antlr4::TokenStream *input, const antlr4::atn::ParserATNSimulatorOptions &options);

  ~OpenCMLParser() override;

  std::string getGrammarFileName() const override;

  const antlr4::atn::ATN& getATN() const override;

  const std::vector<std::string>& getRuleNames() const override;

  const antlr4::dfa::Vocabulary& getVocabulary() const override;

  antlr4::atn::SerializedATNView getSerializedATN() const override;


  class ProgramContext;
  class StmtListContext;
  class StmtContext;
  class LetStmtContext;
  class UseStmtContext;
  class TypeStmtContext;
  class ExprStmtContext;
  class WaitStmtContext;
  class WithDefContext;
  class FuncDefContext;
  class RetStmtContext;
  class LambdaExprContext;
  class CarrierContext;
  class AnnotationContext;
  class AnnotationsContext;
  class ModifiersContext;
  class KeyTypePairContext;
  class KeyValuePairContext;
  class KeyParamPairContext;
  class IndexKTPairContext;
  class IndexKVPairContext;
  class TypeListContext;
  class IdentListContext;
  class ValueListContext;
  class PairedTypesContext;
  class PairedValuesContext;
  class PairedParamsContext;
  class IndexKVPairsContext;
  class ArgumentListContext;
  class BracedPairedValuesContext;
  class BracedIdentsContext;
  class BracedStmtsContext;
  class BracedHomoValuesContext;
  class BracedIndexKVPairsContext;
  class BracketIdentsContext;
  class BracketHomoValuesContext;
  class BracketHeteValuesContext;
  class ParentParamsContext;
  class ParentValuesContext;
  class ParentHomoValuesContext;
  class ParentHeteValuesContext;
  class AngledParamsContext;
  class AngledValuesContext;
  class PrimEntityContext;
  class MemberAccessContext;
  class EntityLinkContext;
  class EntityUnitContext;
  class EntityWithContext;
  class EntityExprContext;
  class RelaExprContext;
  class AddExprContext;
  class MultiExprContext;
  class UnaryExprContext;
  class PrimExprContext;
  class LiteralContext;
  class TypeExprContext;
  class UnaryTypeContext;
  class AtomTypeContext;
  class LambdaTypeContext;
  class PrimTypeContext;
  class StructTypeContext;
  class SpecialTypeContext;
  class IdentRefContext; 

  class  ProgramContext : public antlr4::RuleContextWithAltNum {
  public:
    ProgramContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *EOF();
    StmtListContext *stmtList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ProgramContext* program();

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

  class  StmtContext : public antlr4::RuleContextWithAltNum {
  public:
    StmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    LetStmtContext *letStmt();
    UseStmtContext *useStmt();
    TypeStmtContext *typeStmt();
    ExprStmtContext *exprStmt();
    WaitStmtContext *waitStmt();
    FuncDefContext *funcDef();
    RetStmtContext *retStmt();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  StmtContext* stmt();

  class  LetStmtContext : public antlr4::RuleContextWithAltNum {
  public:
    LetStmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    CarrierContext *carrier();
    EntityExprContext *entityExpr();
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
    CarrierContext *carrier();
    EntityExprContext *entityExpr();
    antlr4::tree::TerminalNode *FROM();
    antlr4::tree::TerminalNode *STRING();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  UseStmtContext* useStmt();

  class  TypeStmtContext : public antlr4::RuleContextWithAltNum {
  public:
    TypeStmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *TYPE();
    IdentRefContext *identRef();
    TypeExprContext *typeExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  TypeStmtContext* typeStmt();

  class  ExprStmtContext : public antlr4::RuleContextWithAltNum {
  public:
    ExprStmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    EntityExprContext *entityExpr();
    AnnotationsContext *annotations();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ExprStmtContext* exprStmt();

  class  WaitStmtContext : public antlr4::RuleContextWithAltNum {
  public:
    WaitStmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *WAIT();
    EntityExprContext *entityExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  WaitStmtContext* waitStmt();

  class  WithDefContext : public antlr4::RuleContextWithAltNum {
  public:
    WithDefContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *WITH();
    AngledParamsContext *angledParams();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  WithDefContext* withDef();

  class  FuncDefContext : public antlr4::RuleContextWithAltNum {
  public:
    FuncDefContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *FUNC();
    IdentRefContext *identRef();
    ParentParamsContext *parentParams();
    BracedStmtsContext *bracedStmts();
    AnnotationsContext *annotations();
    WithDefContext *withDef();
    ModifiersContext *modifiers();
    TypeExprContext *typeExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  FuncDefContext* funcDef();

  class  RetStmtContext : public antlr4::RuleContextWithAltNum {
  public:
    RetStmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *RETURN();
    EntityExprContext *entityExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  RetStmtContext* retStmt();

  class  LambdaExprContext : public antlr4::RuleContextWithAltNum {
  public:
    LambdaExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ParentParamsContext *parentParams();
    BracedStmtsContext *bracedStmts();
    EntityExprContext *entityExpr();
    ModifiersContext *modifiers();
    TypeExprContext *typeExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  LambdaExprContext* lambdaExpr();

  class  CarrierContext : public antlr4::RuleContextWithAltNum {
  public:
    CarrierContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentRefContext *identRef();
    BracedIdentsContext *bracedIdents();
    BracketIdentsContext *bracketIdents();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  CarrierContext* carrier();

  class  AnnotationContext : public antlr4::RuleContextWithAltNum {
  public:
    AnnotationContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    PrimEntityContext *primEntity();


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
    std::vector<antlr4::tree::TerminalNode *> STATIC();
    antlr4::tree::TerminalNode* STATIC(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ModifiersContext* modifiers();

  class  KeyTypePairContext : public antlr4::RuleContextWithAltNum {
  public:
    KeyTypePairContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentRefContext *identRef();
    TypeExprContext *typeExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  KeyTypePairContext* keyTypePair();

  class  KeyValuePairContext : public antlr4::RuleContextWithAltNum {
  public:
    KeyValuePairContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentRefContext *identRef();
    EntityExprContext *entityExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  KeyValuePairContext* keyValuePair();

  class  KeyParamPairContext : public antlr4::RuleContextWithAltNum {
  public:
    KeyParamPairContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentRefContext *identRef();
    TypeExprContext *typeExpr();
    antlr4::tree::TerminalNode *VAR();
    AnnotationContext *annotation();
    EntityExprContext *entityExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  KeyParamPairContext* keyParamPair();

  class  IndexKTPairContext : public antlr4::RuleContextWithAltNum {
  public:
    IndexKTPairContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    EntityExprContext *entityExpr();
    TypeExprContext *typeExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  IndexKTPairContext* indexKTPair();

  class  IndexKVPairContext : public antlr4::RuleContextWithAltNum {
  public:
    IndexKVPairContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<EntityExprContext *> entityExpr();
    EntityExprContext* entityExpr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  IndexKVPairContext* indexKVPair();

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
    std::vector<IdentRefContext *> identRef();
    IdentRefContext* identRef(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  IdentListContext* identList();

  class  ValueListContext : public antlr4::RuleContextWithAltNum {
  public:
    ValueListContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<EntityExprContext *> entityExpr();
    EntityExprContext* entityExpr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ValueListContext* valueList();

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

  class  IndexKVPairsContext : public antlr4::RuleContextWithAltNum {
  public:
    IndexKVPairsContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<IndexKVPairContext *> indexKVPair();
    IndexKVPairContext* indexKVPair(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  IndexKVPairsContext* indexKVPairs();

  class  ArgumentListContext : public antlr4::RuleContextWithAltNum {
  public:
    ArgumentListContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ValueListContext *valueList();
    PairedValuesContext *pairedValues();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ArgumentListContext* argumentList();

  class  BracedPairedValuesContext : public antlr4::RuleContextWithAltNum {
  public:
    BracedPairedValuesContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    PairedValuesContext *pairedValues();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  BracedPairedValuesContext* bracedPairedValues();

  class  BracedIdentsContext : public antlr4::RuleContextWithAltNum {
  public:
    BracedIdentsContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentListContext *identList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  BracedIdentsContext* bracedIdents();

  class  BracedStmtsContext : public antlr4::RuleContextWithAltNum {
  public:
    BracedStmtsContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    StmtListContext *stmtList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  BracedStmtsContext* bracedStmts();

  class  BracedHomoValuesContext : public antlr4::RuleContextWithAltNum {
  public:
    BracedHomoValuesContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ValueListContext *valueList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  BracedHomoValuesContext* bracedHomoValues();

  class  BracedIndexKVPairsContext : public antlr4::RuleContextWithAltNum {
  public:
    BracedIndexKVPairsContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IndexKVPairsContext *indexKVPairs();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  BracedIndexKVPairsContext* bracedIndexKVPairs();

  class  BracketIdentsContext : public antlr4::RuleContextWithAltNum {
  public:
    BracketIdentsContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentListContext *identList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  BracketIdentsContext* bracketIdents();

  class  BracketHomoValuesContext : public antlr4::RuleContextWithAltNum {
  public:
    BracketHomoValuesContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ValueListContext *valueList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  BracketHomoValuesContext* bracketHomoValues();

  class  BracketHeteValuesContext : public antlr4::RuleContextWithAltNum {
  public:
    BracketHeteValuesContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ValueListContext *valueList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  BracketHeteValuesContext* bracketHeteValues();

  class  ParentParamsContext : public antlr4::RuleContextWithAltNum {
  public:
    ParentParamsContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    PairedParamsContext *pairedParams();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ParentParamsContext* parentParams();

  class  ParentValuesContext : public antlr4::RuleContextWithAltNum {
  public:
    ParentValuesContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ArgumentListContext *argumentList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ParentValuesContext* parentValues();

  class  ParentHomoValuesContext : public antlr4::RuleContextWithAltNum {
  public:
    ParentHomoValuesContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ValueListContext *valueList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ParentHomoValuesContext* parentHomoValues();

  class  ParentHeteValuesContext : public antlr4::RuleContextWithAltNum {
  public:
    ParentHeteValuesContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ValueListContext *valueList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ParentHeteValuesContext* parentHeteValues();

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

  class  PrimEntityContext : public antlr4::RuleContextWithAltNum {
  public:
    PrimEntityContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentRefContext *identRef();
    LiteralContext *literal();
    BracedIndexKVPairsContext *bracedIndexKVPairs();
    BracedHomoValuesContext *bracedHomoValues();
    BracedPairedValuesContext *bracedPairedValues();
    BracketHeteValuesContext *bracketHeteValues();
    BracketHomoValuesContext *bracketHomoValues();
    EntityExprContext *entityExpr();
    ParentHeteValuesContext *parentHeteValues();
    ParentHomoValuesContext *parentHomoValues();
    LambdaExprContext *lambdaExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  PrimEntityContext* primEntity();

  class  MemberAccessContext : public antlr4::RuleContextWithAltNum {
  public:
    MemberAccessContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    EntityExprContext *entityExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  MemberAccessContext* memberAccess();

  class  EntityLinkContext : public antlr4::RuleContextWithAltNum {
  public:
    EntityLinkContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    EntityUnitContext *entityUnit();
    EntityLinkContext *entityLink();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  EntityLinkContext* entityLink();
  EntityLinkContext* entityLink(int precedence);
  class  EntityUnitContext : public antlr4::RuleContextWithAltNum {
  public:
    EntityUnitContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    EntityWithContext *entityWith();
    std::vector<AnnotationContext *> annotation();
    AnnotationContext* annotation(size_t i);
    std::vector<MemberAccessContext *> memberAccess();
    MemberAccessContext* memberAccess(size_t i);
    std::vector<AngledValuesContext *> angledValues();
    AngledValuesContext* angledValues(size_t i);
    std::vector<ParentValuesContext *> parentValues();
    ParentValuesContext* parentValues(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  EntityUnitContext* entityUnit();

  class  EntityWithContext : public antlr4::RuleContextWithAltNum {
  public:
    EntityWithContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    PrimEntityContext *primEntity();
    EntityWithContext *entityWith();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  EntityWithContext* entityWith();
  EntityWithContext* entityWith(int precedence);
  class  EntityExprContext : public antlr4::RuleContextWithAltNum {
  public:
    EntityExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    RelaExprContext *relaExpr();
    std::vector<EntityExprContext *> entityExpr();
    EntityExprContext* entityExpr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  EntityExprContext* entityExpr();
  EntityExprContext* entityExpr(int precedence);
  class  RelaExprContext : public antlr4::RuleContextWithAltNum {
  public:
    RelaExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    AddExprContext *addExpr();
    RelaExprContext *relaExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  RelaExprContext* relaExpr();
  RelaExprContext* relaExpr(int precedence);
  class  AddExprContext : public antlr4::RuleContextWithAltNum {
  public:
    AddExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    MultiExprContext *multiExpr();
    AddExprContext *addExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  AddExprContext* addExpr();
  AddExprContext* addExpr(int precedence);
  class  MultiExprContext : public antlr4::RuleContextWithAltNum {
  public:
    MultiExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    UnaryExprContext *unaryExpr();
    MultiExprContext *multiExpr();
    antlr4::tree::TerminalNode *AS();
    TypeExprContext *typeExpr();
    antlr4::tree::TerminalNode *IS();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  MultiExprContext* multiExpr();
  MultiExprContext* multiExpr(int precedence);
  class  UnaryExprContext : public antlr4::RuleContextWithAltNum {
  public:
    UnaryExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    PrimExprContext *primExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  UnaryExprContext* unaryExpr();

  class  PrimExprContext : public antlr4::RuleContextWithAltNum {
  public:
    PrimExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    EntityLinkContext *entityLink();
    EntityExprContext *entityExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  PrimExprContext* primExpr();

  class  LiteralContext : public antlr4::RuleContextWithAltNum {
  public:
    LiteralContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *INTEGER();
    antlr4::tree::TerminalNode *UNIT();
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
    UnaryTypeContext *unaryType();
    TypeExprContext *typeExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  TypeExprContext* typeExpr();
  TypeExprContext* typeExpr(int precedence);
  class  UnaryTypeContext : public antlr4::RuleContextWithAltNum {
  public:
    UnaryTypeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    AtomTypeContext *atomType();
    std::vector<antlr4::tree::TerminalNode *> INTEGER();
    antlr4::tree::TerminalNode* INTEGER(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  UnaryTypeContext* unaryType();

  class  AtomTypeContext : public antlr4::RuleContextWithAltNum {
  public:
    AtomTypeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    PrimTypeContext *primType();
    StructTypeContext *structType();
    SpecialTypeContext *specialType();
    IdentRefContext *identRef();
    TypeExprContext *typeExpr();
    LambdaTypeContext *lambdaType();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  AtomTypeContext* atomType();

  class  LambdaTypeContext : public antlr4::RuleContextWithAltNum {
  public:
    LambdaTypeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    TypeExprContext *typeExpr();
    std::vector<PairedParamsContext *> pairedParams();
    PairedParamsContext* pairedParams(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  LambdaTypeContext* lambdaType();

  class  PrimTypeContext : public antlr4::RuleContextWithAltNum {
  public:
    PrimTypeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *INTEGER_TYPE();
    antlr4::tree::TerminalNode *INTEGER32_TYPE();
    antlr4::tree::TerminalNode *INTEGER64_TYPE();
    antlr4::tree::TerminalNode *REAL_TYPE();
    antlr4::tree::TerminalNode *FLOAT_TYPE();
    antlr4::tree::TerminalNode *DOUBLE_TYPE();
    antlr4::tree::TerminalNode *NUMBER_TYPE();
    antlr4::tree::TerminalNode *STRING_TYPE();
    antlr4::tree::TerminalNode *BOOL_TYPE();
    antlr4::tree::TerminalNode *CHAR_TYPE();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  PrimTypeContext* primType();

  class  StructTypeContext : public antlr4::RuleContextWithAltNum {
  public:
    StructTypeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *SET_TYPE();
    std::vector<TypeExprContext *> typeExpr();
    TypeExprContext* typeExpr(size_t i);
    antlr4::tree::TerminalNode *MAP_TYPE();
    antlr4::tree::TerminalNode *LIST_TYPE();
    antlr4::tree::TerminalNode *DICT_TYPE();
    antlr4::tree::TerminalNode *ARRAY_TYPE();
    antlr4::tree::TerminalNode *TUPLE_TYPE();
    TypeListContext *typeList();
    antlr4::tree::TerminalNode *UNION_TYPE();
    antlr4::tree::TerminalNode *VECTOR_TYPE();
    std::vector<antlr4::tree::TerminalNode *> INTEGER();
    antlr4::tree::TerminalNode* INTEGER(size_t i);
    antlr4::tree::TerminalNode *TENSOR_TYPE();
    PairedTypesContext *pairedTypes();
    IndexKTPairContext *indexKTPair();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  StructTypeContext* structType();

  class  SpecialTypeContext : public antlr4::RuleContextWithAltNum {
  public:
    SpecialTypeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *ANY_TYPE();
    antlr4::tree::TerminalNode *VOID_TYPE();
    antlr4::tree::TerminalNode *FUNCTOR_TYPE();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  SpecialTypeContext* specialType();

  class  IdentRefContext : public antlr4::RuleContextWithAltNum {
  public:
    IdentRefContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *IDENTIFIER();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  IdentRefContext* identRef();


  bool sempred(antlr4::RuleContext *_localctx, size_t ruleIndex, size_t predicateIndex) override;

  bool entityLinkSempred(EntityLinkContext *_localctx, size_t predicateIndex);
  bool entityWithSempred(EntityWithContext *_localctx, size_t predicateIndex);
  bool entityExprSempred(EntityExprContext *_localctx, size_t predicateIndex);
  bool relaExprSempred(RelaExprContext *_localctx, size_t predicateIndex);
  bool addExprSempred(AddExprContext *_localctx, size_t predicateIndex);
  bool multiExprSempred(MultiExprContext *_localctx, size_t predicateIndex);
  bool typeExprSempred(TypeExprContext *_localctx, size_t predicateIndex);

  // By default the static state used to implement the parser is lazily initialized during the first
  // call to the constructor. You can call this function if you wish to initialize the static state
  // ahead of time.
  static void initialize();

private:
};

