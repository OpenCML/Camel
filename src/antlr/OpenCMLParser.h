
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
    SEP = 39, AS = 40, IS = 41, LET = 42, USE = 43, FUNC = 44, TYPE = 45, 
    WITH = 46, RETURN = 47, INNER = 48, OUTER = 49, ATOMIC = 50, STATIC = 51, 
    SYNC = 52, NULL_ = 53, TRUE = 54, FALSE = 55, INTEGER_TYPE = 56, INTEGER32_TYPE = 57, 
    INTEGER64_TYPE = 58, REAL_TYPE = 59, FLOAT_TYPE = 60, DOUBLE_TYPE = 61, 
    NUMBER_TYPE = 62, STRING_TYPE = 63, BOOL_TYPE = 64, CHAR_TYPE = 65, 
    SET_TYPE = 66, MAP_TYPE = 67, LIST_TYPE = 68, DICT_TYPE = 69, ARRAY_TYPE = 70, 
    TUPLE_TYPE = 71, VECTOR_TYPE = 72, TENSOR_TYPE = 73, ANY_TYPE = 74, 
    VOID_TYPE = 75, FUNCTOR_TYPE = 76, SKIP_ = 77, MULTI_STR = 78, IDENTIFIER = 79, 
    UNIT = 80, STRING = 81, FSTRING = 82, INTEGER = 83, REAL = 84
  };

  enum {
    RuleProgram = 0, RuleStmtList = 1, RuleStmt = 2, RuleLetStmt = 3, RuleUseStmt = 4, 
    RuleTypeStmt = 5, RuleExprStmt = 6, RuleAssignStmt = 7, RuleWithDef = 8, 
    RuleFuncDef = 9, RuleRetStmt = 10, RuleLambda = 11, RuleCarrier = 12, 
    RuleAnnotation = 13, RuleAnnotations = 14, RuleModifiers = 15, RuleKeyTypePair = 16, 
    RuleKeyValuePair = 17, RuleKeyParamPair = 18, RuleIndexKTPair = 19, 
    RuleIndexKVPair = 20, RuleTypeList = 21, RuleIdentList = 22, RuleValueList = 23, 
    RulePairedTypes = 24, RulePairedValues = 25, RulePairedParams = 26, 
    RuleIndexKVPairs = 27, RuleArgumentList = 28, RuleBracedValues = 29, 
    RuleBracedIndexKVPairs = 30, RuleBracedPairedValues = 31, RuleBracedIdents = 32, 
    RuleBracedStmts = 33, RuleBracketIdents = 34, RuleBracketValues = 35, 
    RuleParentParams = 36, RuleParentValues = 37, RuleAngledParams = 38, 
    RuleAngledValues = 39, RulePrimEntity = 40, RuleMemberAccess = 41, RuleEntity = 42, 
    RuleEntityLink = 43, RuleEntityChain = 44, RuleEntitySpread = 45, RuleEntityExpr = 46, 
    RuleRelaExpr = 47, RuleAddExpr = 48, RuleMultiExpr = 49, RuleUnaryExpr = 50, 
    RulePrimExpr = 51, RuleLiteral = 52, RuleTypeExpr = 53, RuleType = 54, 
    RulePrimType = 55, RuleStructType = 56, RuleSpecialType = 57, RuleIdentRef = 58
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
  class AssignStmtContext;
  class WithDefContext;
  class FuncDefContext;
  class RetStmtContext;
  class LambdaContext;
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
  class BracedValuesContext;
  class BracedIndexKVPairsContext;
  class BracedPairedValuesContext;
  class BracedIdentsContext;
  class BracedStmtsContext;
  class BracketIdentsContext;
  class BracketValuesContext;
  class ParentParamsContext;
  class ParentValuesContext;
  class AngledParamsContext;
  class AngledValuesContext;
  class PrimEntityContext;
  class MemberAccessContext;
  class EntityContext;
  class EntityLinkContext;
  class EntityChainContext;
  class EntitySpreadContext;
  class EntityExprContext;
  class RelaExprContext;
  class AddExprContext;
  class MultiExprContext;
  class UnaryExprContext;
  class PrimExprContext;
  class LiteralContext;
  class TypeExprContext;
  class TypeContext;
  class PrimTypeContext;
  class StructTypeContext;
  class SpecialTypeContext;
  class IdentRefContext; 

  class  ProgramContext : public antlr4::ParserRuleContext {
  public:
    ProgramContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *EOF();
    StmtListContext *stmtList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ProgramContext* program();

  class  StmtListContext : public antlr4::ParserRuleContext {
  public:
    StmtListContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<StmtContext *> stmt();
    StmtContext* stmt(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  StmtListContext* stmtList();

  class  StmtContext : public antlr4::ParserRuleContext {
  public:
    StmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    LetStmtContext *letStmt();
    antlr4::tree::TerminalNode *SEP();
    UseStmtContext *useStmt();
    TypeStmtContext *typeStmt();
    ExprStmtContext *exprStmt();
    AssignStmtContext *assignStmt();
    FuncDefContext *funcDef();
    RetStmtContext *retStmt();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  StmtContext* stmt();

  class  LetStmtContext : public antlr4::ParserRuleContext {
  public:
    LetStmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *LET();
    CarrierContext *carrier();
    EntityExprContext *entityExpr();
    TypeExprContext *typeExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  LetStmtContext* letStmt();

  class  UseStmtContext : public antlr4::ParserRuleContext {
  public:
    UseStmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *USE();
    CarrierContext *carrier();
    EntityExprContext *entityExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  UseStmtContext* useStmt();

  class  TypeStmtContext : public antlr4::ParserRuleContext {
  public:
    TypeStmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *TYPE();
    IdentRefContext *identRef();
    TypeExprContext *typeExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  TypeStmtContext* typeStmt();

  class  ExprStmtContext : public antlr4::ParserRuleContext {
  public:
    ExprStmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    EntityExprContext *entityExpr();
    AnnotationsContext *annotations();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ExprStmtContext* exprStmt();

  class  AssignStmtContext : public antlr4::ParserRuleContext {
  public:
    AssignStmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentRefContext *identRef();
    EntityExprContext *entityExpr();
    MemberAccessContext *memberAccess();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  AssignStmtContext* assignStmt();

  class  WithDefContext : public antlr4::ParserRuleContext {
  public:
    WithDefContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *WITH();
    AngledParamsContext *angledParams();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  WithDefContext* withDef();

  class  FuncDefContext : public antlr4::ParserRuleContext {
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

  class  RetStmtContext : public antlr4::ParserRuleContext {
  public:
    RetStmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *RETURN();
    EntityExprContext *entityExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  RetStmtContext* retStmt();

  class  LambdaContext : public antlr4::ParserRuleContext {
  public:
    LambdaContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    BracedStmtsContext *bracedStmts();
    ModifiersContext *modifiers();
    ParentParamsContext *parentParams();
    TypeExprContext *typeExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  LambdaContext* lambda();

  class  CarrierContext : public antlr4::ParserRuleContext {
  public:
    CarrierContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentRefContext *identRef();
    BracedIdentsContext *bracedIdents();
    BracketIdentsContext *bracketIdents();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  CarrierContext* carrier();

  class  AnnotationContext : public antlr4::ParserRuleContext {
  public:
    AnnotationContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    EntityExprContext *entityExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  AnnotationContext* annotation();

  class  AnnotationsContext : public antlr4::ParserRuleContext {
  public:
    AnnotationsContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<AnnotationContext *> annotation();
    AnnotationContext* annotation(size_t i);
    std::vector<antlr4::tree::TerminalNode *> SEP();
    antlr4::tree::TerminalNode* SEP(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  AnnotationsContext* annotations();

  class  ModifiersContext : public antlr4::ParserRuleContext {
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

  class  KeyTypePairContext : public antlr4::ParserRuleContext {
  public:
    KeyTypePairContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentRefContext *identRef();
    TypeExprContext *typeExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  KeyTypePairContext* keyTypePair();

  class  KeyValuePairContext : public antlr4::ParserRuleContext {
  public:
    KeyValuePairContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<EntityExprContext *> entityExpr();
    EntityExprContext* entityExpr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  KeyValuePairContext* keyValuePair();

  class  KeyParamPairContext : public antlr4::ParserRuleContext {
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

  class  IndexKTPairContext : public antlr4::ParserRuleContext {
  public:
    IndexKTPairContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    EntityExprContext *entityExpr();
    TypeExprContext *typeExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  IndexKTPairContext* indexKTPair();

  class  IndexKVPairContext : public antlr4::ParserRuleContext {
  public:
    IndexKVPairContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<EntityExprContext *> entityExpr();
    EntityExprContext* entityExpr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  IndexKVPairContext* indexKVPair();

  class  TypeListContext : public antlr4::ParserRuleContext {
  public:
    TypeListContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<TypeExprContext *> typeExpr();
    TypeExprContext* typeExpr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  TypeListContext* typeList();

  class  IdentListContext : public antlr4::ParserRuleContext {
  public:
    IdentListContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<IdentRefContext *> identRef();
    IdentRefContext* identRef(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  IdentListContext* identList();

  class  ValueListContext : public antlr4::ParserRuleContext {
  public:
    ValueListContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<EntityExprContext *> entityExpr();
    EntityExprContext* entityExpr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ValueListContext* valueList();

  class  PairedTypesContext : public antlr4::ParserRuleContext {
  public:
    PairedTypesContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<KeyTypePairContext *> keyTypePair();
    KeyTypePairContext* keyTypePair(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  PairedTypesContext* pairedTypes();

  class  PairedValuesContext : public antlr4::ParserRuleContext {
  public:
    PairedValuesContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<KeyValuePairContext *> keyValuePair();
    KeyValuePairContext* keyValuePair(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  PairedValuesContext* pairedValues();

  class  PairedParamsContext : public antlr4::ParserRuleContext {
  public:
    PairedParamsContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<KeyParamPairContext *> keyParamPair();
    KeyParamPairContext* keyParamPair(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  PairedParamsContext* pairedParams();

  class  IndexKVPairsContext : public antlr4::ParserRuleContext {
  public:
    IndexKVPairsContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<IndexKVPairContext *> indexKVPair();
    IndexKVPairContext* indexKVPair(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  IndexKVPairsContext* indexKVPairs();

  class  ArgumentListContext : public antlr4::ParserRuleContext {
  public:
    ArgumentListContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ValueListContext *valueList();
    PairedValuesContext *pairedValues();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ArgumentListContext* argumentList();

  class  BracedValuesContext : public antlr4::ParserRuleContext {
  public:
    BracedValuesContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ValueListContext *valueList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  BracedValuesContext* bracedValues();

  class  BracedIndexKVPairsContext : public antlr4::ParserRuleContext {
  public:
    BracedIndexKVPairsContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IndexKVPairsContext *indexKVPairs();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  BracedIndexKVPairsContext* bracedIndexKVPairs();

  class  BracedPairedValuesContext : public antlr4::ParserRuleContext {
  public:
    BracedPairedValuesContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    PairedValuesContext *pairedValues();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  BracedPairedValuesContext* bracedPairedValues();

  class  BracedIdentsContext : public antlr4::ParserRuleContext {
  public:
    BracedIdentsContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentListContext *identList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  BracedIdentsContext* bracedIdents();

  class  BracedStmtsContext : public antlr4::ParserRuleContext {
  public:
    BracedStmtsContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    StmtListContext *stmtList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  BracedStmtsContext* bracedStmts();

  class  BracketIdentsContext : public antlr4::ParserRuleContext {
  public:
    BracketIdentsContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentListContext *identList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  BracketIdentsContext* bracketIdents();

  class  BracketValuesContext : public antlr4::ParserRuleContext {
  public:
    BracketValuesContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ValueListContext *valueList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  BracketValuesContext* bracketValues();

  class  ParentParamsContext : public antlr4::ParserRuleContext {
  public:
    ParentParamsContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    PairedParamsContext *pairedParams();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ParentParamsContext* parentParams();

  class  ParentValuesContext : public antlr4::ParserRuleContext {
  public:
    ParentValuesContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ArgumentListContext *argumentList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ParentValuesContext* parentValues();

  class  AngledParamsContext : public antlr4::ParserRuleContext {
  public:
    AngledParamsContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    PairedParamsContext *pairedParams();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  AngledParamsContext* angledParams();

  class  AngledValuesContext : public antlr4::ParserRuleContext {
  public:
    AngledValuesContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ArgumentListContext *argumentList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  AngledValuesContext* angledValues();

  class  PrimEntityContext : public antlr4::ParserRuleContext {
  public:
    PrimEntityContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentRefContext *identRef();
    LiteralContext *literal();
    BracketValuesContext *bracketValues();
    BracedValuesContext *bracedValues();
    BracedPairedValuesContext *bracedPairedValues();
    BracedIndexKVPairsContext *bracedIndexKVPairs();
    LambdaContext *lambda();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  PrimEntityContext* primEntity();

  class  MemberAccessContext : public antlr4::ParserRuleContext {
  public:
    MemberAccessContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<IdentRefContext *> identRef();
    IdentRefContext* identRef(size_t i);
    std::vector<antlr4::tree::TerminalNode *> INTEGER();
    antlr4::tree::TerminalNode* INTEGER(size_t i);
    std::vector<EntityExprContext *> entityExpr();
    EntityExprContext* entityExpr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  MemberAccessContext* memberAccess();

  class  EntityContext : public antlr4::ParserRuleContext {
  public:
    EntityContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    PrimEntityContext *primEntity();
    MemberAccessContext *memberAccess();
    AngledValuesContext *angledValues();
    AnnotationContext *annotation();
    ParentValuesContext *parentValues();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  EntityContext* entity();

  class  EntityLinkContext : public antlr4::ParserRuleContext {
  public:
    EntityLinkContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    EntityContext *entity();
    EntityLinkContext *entityLink();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  EntityLinkContext* entityLink();
  EntityLinkContext* entityLink(int precedence);
  class  EntityChainContext : public antlr4::ParserRuleContext {
  public:
    EntityChainContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<EntityLinkContext *> entityLink();
    EntityLinkContext* entityLink(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  EntityChainContext* entityChain();

  class  EntitySpreadContext : public antlr4::ParserRuleContext {
  public:
    EntitySpreadContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    EntityContext *entity();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  EntitySpreadContext* entitySpread();

  class  EntityExprContext : public antlr4::ParserRuleContext {
  public:
    EntityExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    RelaExprContext *relaExpr();
    EntityExprContext *entityExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  EntityExprContext* entityExpr();
  EntityExprContext* entityExpr(int precedence);
  class  RelaExprContext : public antlr4::ParserRuleContext {
  public:
    RelaExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    AddExprContext *addExpr();
    RelaExprContext *relaExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  RelaExprContext* relaExpr();
  RelaExprContext* relaExpr(int precedence);
  class  AddExprContext : public antlr4::ParserRuleContext {
  public:
    AddExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    MultiExprContext *multiExpr();
    AddExprContext *addExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  AddExprContext* addExpr();
  AddExprContext* addExpr(int precedence);
  class  MultiExprContext : public antlr4::ParserRuleContext {
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
  class  UnaryExprContext : public antlr4::ParserRuleContext {
  public:
    UnaryExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    PrimExprContext *primExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  UnaryExprContext* unaryExpr();

  class  PrimExprContext : public antlr4::ParserRuleContext {
  public:
    PrimExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    EntityChainContext *entityChain();
    EntityExprContext *entityExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  PrimExprContext* primExpr();

  class  LiteralContext : public antlr4::ParserRuleContext {
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

  class  TypeExprContext : public antlr4::ParserRuleContext {
  public:
    TypeExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    TypeContext *type();
    std::vector<TypeExprContext *> typeExpr();
    TypeExprContext* typeExpr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  TypeExprContext* typeExpr();
  TypeExprContext* typeExpr(int precedence);
  class  TypeContext : public antlr4::ParserRuleContext {
  public:
    TypeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    PrimTypeContext *primType();
    StructTypeContext *structType();
    SpecialTypeContext *specialType();
    IdentRefContext *identRef();
    TypeExprContext *typeExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  TypeContext* type();

  class  PrimTypeContext : public antlr4::ParserRuleContext {
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

  class  StructTypeContext : public antlr4::ParserRuleContext {
  public:
    StructTypeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *SET_TYPE();
    std::vector<TypeExprContext *> typeExpr();
    TypeExprContext* typeExpr(size_t i);
    antlr4::tree::TerminalNode *MAP_TYPE();
    antlr4::tree::TerminalNode *LIST_TYPE();
    antlr4::tree::TerminalNode *DICT_TYPE();
    PairedTypesContext *pairedTypes();
    antlr4::tree::TerminalNode *ARRAY_TYPE();
    antlr4::tree::TerminalNode *TUPLE_TYPE();
    TypeListContext *typeList();
    antlr4::tree::TerminalNode *VECTOR_TYPE();
    std::vector<antlr4::tree::TerminalNode *> INTEGER();
    antlr4::tree::TerminalNode* INTEGER(size_t i);
    antlr4::tree::TerminalNode *TENSOR_TYPE();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  StructTypeContext* structType();

  class  SpecialTypeContext : public antlr4::ParserRuleContext {
  public:
    SpecialTypeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *ANY_TYPE();
    antlr4::tree::TerminalNode *VOID_TYPE();
    antlr4::tree::TerminalNode *FUNCTOR_TYPE();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  SpecialTypeContext* specialType();

  class  IdentRefContext : public antlr4::ParserRuleContext {
  public:
    IdentRefContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *IDENTIFIER();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  IdentRefContext* identRef();


  bool sempred(antlr4::RuleContext *_localctx, size_t ruleIndex, size_t predicateIndex) override;

  bool entityLinkSempred(EntityLinkContext *_localctx, size_t predicateIndex);
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

