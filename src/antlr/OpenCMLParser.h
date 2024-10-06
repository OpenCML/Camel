
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
    T__38 = 39, T__39 = 40, SEP = 41, AS = 42, IS = 43, LET = 44, VAR = 45, 
    USE = 46, FROM = 47, FUNC = 48, TYPE = 49, WITH = 50, WAIT = 51, RETURN = 52, 
    INNER = 53, OUTER = 54, ATOMIC = 55, STATIC = 56, SYNC = 57, NULL_ = 58, 
    TRUE = 59, FALSE = 60, INTEGER_TYPE = 61, INTEGER32_TYPE = 62, INTEGER64_TYPE = 63, 
    REAL_TYPE = 64, FLOAT_TYPE = 65, DOUBLE_TYPE = 66, NUMBER_TYPE = 67, 
    STRING_TYPE = 68, BOOL_TYPE = 69, CHAR_TYPE = 70, SET_TYPE = 71, MAP_TYPE = 72, 
    LIST_TYPE = 73, DICT_TYPE = 74, ARRAY_TYPE = 75, TUPLE_TYPE = 76, UNION_TYPE = 77, 
    VECTOR_TYPE = 78, TENSOR_TYPE = 79, ANY_TYPE = 80, VOID_TYPE = 81, FUNCTOR_TYPE = 82, 
    BLANK = 83, LIN_CMT_HASH = 84, LIN_CMT_SLASH = 85, BLK_CMT = 86, MULTI_STR = 87, 
    IDENTIFIER = 88, UNIT = 89, STRING = 90, FSTRING = 91, INTEGER = 92, 
    REAL = 93
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
    RuleBracedIdents = 30, RuleBracedStmts = 31, RuleBracedValues = 32, 
    RuleBracedIndexKVPairs = 33, RuleBracketIdents = 34, RuleBracketValues = 35, 
    RuleMemberAccess = 36, RuleParentParams = 37, RuleParentArgues = 38, 
    RuleParentValues = 39, RuleAngledParams = 40, RuleAngledValues = 41, 
    RuleEntityExpr = 42, RuleTernaryExpr = 43, RuleLogicalOrExpr = 44, RuleLogicalAndExpr = 45, 
    RuleEqualityExpr = 46, RuleRelationalExpr = 47, RuleAdditiveExpr = 48, 
    RuleMultiplicativeExpr = 49, RuleUnaryExpr = 50, RuleLinkExpr = 51, 
    RuleWithExpr = 52, RuleAnnotatedExpr = 53, RulePrimaryExpr = 54, RuleLiteral = 55, 
    RuleTypeExpr = 56, RuleArrayType = 57, RuleAtomType = 58, RuleLambdaType = 59, 
    RulePrimaryType = 60, RuleStructType = 61, RuleSpecialType = 62, RuleIdentRef = 63
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
  class BracedValuesContext;
  class BracedIndexKVPairsContext;
  class BracketIdentsContext;
  class BracketValuesContext;
  class MemberAccessContext;
  class ParentParamsContext;
  class ParentArguesContext;
  class ParentValuesContext;
  class AngledParamsContext;
  class AngledValuesContext;
  class EntityExprContext;
  class TernaryExprContext;
  class LogicalOrExprContext;
  class LogicalAndExprContext;
  class EqualityExprContext;
  class RelationalExprContext;
  class AdditiveExprContext;
  class MultiplicativeExprContext;
  class UnaryExprContext;
  class LinkExprContext;
  class WithExprContext;
  class AnnotatedExprContext;
  class PrimaryExprContext;
  class LiteralContext;
  class TypeExprContext;
  class ArrayTypeContext;
  class AtomTypeContext;
  class LambdaTypeContext;
  class PrimaryTypeContext;
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
    antlr4::tree::TerminalNode *FROM();
    antlr4::tree::TerminalNode *STRING();
    IdentRefContext *identRef();
    BracedIdentsContext *bracedIdents();


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
    AngledParamsContext *angledParams();
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
    PrimaryExprContext *primaryExpr();


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
    std::vector<antlr4::tree::TerminalNode *> SYNC();
    antlr4::tree::TerminalNode* SYNC(size_t i);


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
    AnnotationContext *annotation();
    EntityExprContext *entityExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  KeyParamPairContext* keyParamPair();

  class  IndexKTPairContext : public antlr4::RuleContextWithAltNum {
  public:
    IndexKTPairContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<TypeExprContext *> typeExpr();
    TypeExprContext* typeExpr(size_t i);


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

  class  BracedValuesContext : public antlr4::RuleContextWithAltNum {
  public:
    BracedValuesContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ValueListContext *valueList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  BracedValuesContext* bracedValues();

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

  class  BracketValuesContext : public antlr4::RuleContextWithAltNum {
  public:
    BracketValuesContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ValueListContext *valueList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  BracketValuesContext* bracketValues();

  class  MemberAccessContext : public antlr4::RuleContextWithAltNum {
  public:
    MemberAccessContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    EntityExprContext *entityExpr();


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

  class  EntityExprContext : public antlr4::RuleContextWithAltNum {
  public:
    EntityExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<TernaryExprContext *> ternaryExpr();
    TernaryExprContext* ternaryExpr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  EntityExprContext* entityExpr();

  class  TernaryExprContext : public antlr4::RuleContextWithAltNum {
  public:
    TernaryExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    RelationalExprContext *relationalExpr();
    std::vector<TernaryExprContext *> ternaryExpr();
    TernaryExprContext* ternaryExpr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  TernaryExprContext* ternaryExpr();

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
    std::vector<UnaryExprContext *> unaryExpr();
    UnaryExprContext* unaryExpr(size_t i);
    std::vector<antlr4::tree::TerminalNode *> AS();
    antlr4::tree::TerminalNode* AS(size_t i);
    std::vector<antlr4::tree::TerminalNode *> IS();
    antlr4::tree::TerminalNode* IS(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  MultiplicativeExprContext* multiplicativeExpr();

  class  UnaryExprContext : public antlr4::RuleContextWithAltNum {
  public:
    UnaryExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    LinkExprContext *linkExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  UnaryExprContext* unaryExpr();

  class  LinkExprContext : public antlr4::RuleContextWithAltNum {
  public:
    LinkExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<WithExprContext *> withExpr();
    WithExprContext* withExpr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  LinkExprContext* linkExpr();

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
    PrimaryExprContext *primaryExpr();
    std::vector<MemberAccessContext *> memberAccess();
    MemberAccessContext* memberAccess(size_t i);
    std::vector<ParentArguesContext *> parentArgues();
    ParentArguesContext* parentArgues(size_t i);
    std::vector<AngledValuesContext *> angledValues();
    AngledValuesContext* angledValues(size_t i);
    std::vector<AnnotationContext *> annotation();
    AnnotationContext* annotation(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  AnnotatedExprContext* annotatedExpr();

  class  PrimaryExprContext : public antlr4::RuleContextWithAltNum {
  public:
    PrimaryExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentRefContext *identRef();
    LiteralContext *literal();
    BracketValuesContext *bracketValues();
    BracedPairedValuesContext *bracedPairedValues();
    EntityExprContext *entityExpr();
    ParentValuesContext *parentValues();
    std::vector<TypeExprContext *> typeExpr();
    TypeExprContext* typeExpr(size_t i);
    BracedValuesContext *bracedValues();
    BracedIndexKVPairsContext *bracedIndexKVPairs();
    std::vector<antlr4::tree::TerminalNode *> INTEGER();
    antlr4::tree::TerminalNode* INTEGER(size_t i);
    LambdaExprContext *lambdaExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  PrimaryExprContext* primaryExpr();

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
    std::vector<ArrayTypeContext *> arrayType();
    ArrayTypeContext* arrayType(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  TypeExprContext* typeExpr();

  class  ArrayTypeContext : public antlr4::RuleContextWithAltNum {
  public:
    ArrayTypeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    AtomTypeContext *atomType();
    std::vector<antlr4::tree::TerminalNode *> INTEGER();
    antlr4::tree::TerminalNode* INTEGER(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ArrayTypeContext* arrayType();

  class  AtomTypeContext : public antlr4::RuleContextWithAltNum {
  public:
    AtomTypeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    PrimaryTypeContext *primaryType();
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
    ParentParamsContext *parentParams();
    TypeExprContext *typeExpr();
    ModifiersContext *modifiers();
    AngledParamsContext *angledParams();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  LambdaTypeContext* lambdaType();

  class  PrimaryTypeContext : public antlr4::RuleContextWithAltNum {
  public:
    PrimaryTypeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
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

  PrimaryTypeContext* primaryType();

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
    std::vector<antlr4::tree::TerminalNode *> INTEGER();
    antlr4::tree::TerminalNode* INTEGER(size_t i);
    antlr4::tree::TerminalNode *TUPLE_TYPE();
    TypeListContext *typeList();
    antlr4::tree::TerminalNode *UNION_TYPE();
    antlr4::tree::TerminalNode *VECTOR_TYPE();
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


  // By default the static state used to implement the parser is lazily initialized during the first
  // call to the constructor. You can call this function if you wish to initialize the static state
  // ahead of time.
  static void initialize();

private:
};

