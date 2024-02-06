
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
    T__32 = 33, T__33 = 34, T__34 = 35, T__35 = 36, SEP = 37, AS = 38, LET = 39, 
    USE = 40, FUNC = 41, TYPE = 42, ENUM = 43, WITH = 44, RETURN = 45, INNER = 46, 
    OUTER = 47, SYNC = 48, SCOPED = 49, STATIC = 50, ATOMIC = 51, NULL_ = 52, 
    TRUE = 53, FALSE = 54, ANY_TYPE = 55, NUMBER_TYPE = 56, STRING_TYPE = 57, 
    BOOLEAN_TYPE = 58, FUNCTOR_TYPE = 59, INTEGER_TYPE = 60, REAL_TYPE = 61, 
    COMPLEX_TYPE = 62, ARRAY_TYPE = 63, MATRIX_TYPE = 64, LIST_TYPE = 65, 
    DICT_TYPE = 66, SKIP_ = 67, MULTI_STR = 68, IDENTIFIER = 69, UNIT = 70, 
    STRING = 71, FSTRING = 72, INTEGER = 73, REAL = 74
  };

  enum {
    RuleProgram = 0, RuleStmtList = 1, RuleStmt = 2, RuleLetStmt = 3, RuleUseStmt = 4, 
    RuleWithDef = 5, RuleFuncDef = 6, RuleTypeDef = 7, RuleEnumDef = 8, 
    RuleRetStmt = 9, RuleExprStmt = 10, RuleAssignStmt = 11, RuleCarrier = 12, 
    RuleBiasAnno = 13, RuleSizeAnno = 14, RuleAnnotation = 15, RuleAnnotations = 16, 
    RuleModifiers = 17, RuleWithList = 18, RuleWithDecl = 19, RuleParamDef = 20, 
    RuleArgsList = 21, RuleArgument = 22, RuleTypePack = 23, RuleKeyValDecl = 24, 
    RuleKeyValExpr = 25, RuleEntityRef = 26, RuleFunctorRef = 27, RuleListUnpack = 28, 
    RuleDictUnpack = 29, RuleDictPack = 30, RuleListPack = 31, RuleStmtPack = 32, 
    RuleLambda = 33, RuleEntityExpr = 34, RuleEntityChain = 35, RuleEntity = 36, 
    RuleNormCall = 37, RuleLinkCall = 38, RuleCalcExpr = 39, RuleRelaExpr = 40, 
    RuleAddExpr = 41, RuleMultiExpr = 42, RuleUnaryExpr = 43, RulePrimExpr = 44, 
    RuleExpr = 45, RuleLiteral = 46, RuleValue = 47, RuleType = 48, RuleInnerType = 49, 
    RuleNumberType = 50, RuleScalarType = 51, RuleVectorType = 52, RuleStructType = 53, 
    RuleNullableType = 54, RuleIdentRef = 55
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
  class WithDefContext;
  class FuncDefContext;
  class TypeDefContext;
  class EnumDefContext;
  class RetStmtContext;
  class ExprStmtContext;
  class AssignStmtContext;
  class CarrierContext;
  class BiasAnnoContext;
  class SizeAnnoContext;
  class AnnotationContext;
  class AnnotationsContext;
  class ModifiersContext;
  class WithListContext;
  class WithDeclContext;
  class ParamDefContext;
  class ArgsListContext;
  class ArgumentContext;
  class TypePackContext;
  class KeyValDeclContext;
  class KeyValExprContext;
  class EntityRefContext;
  class FunctorRefContext;
  class ListUnpackContext;
  class DictUnpackContext;
  class DictPackContext;
  class ListPackContext;
  class StmtPackContext;
  class LambdaContext;
  class EntityExprContext;
  class EntityChainContext;
  class EntityContext;
  class NormCallContext;
  class LinkCallContext;
  class CalcExprContext;
  class RelaExprContext;
  class AddExprContext;
  class MultiExprContext;
  class UnaryExprContext;
  class PrimExprContext;
  class ExprContext;
  class LiteralContext;
  class ValueContext;
  class TypeContext;
  class InnerTypeContext;
  class NumberTypeContext;
  class ScalarTypeContext;
  class VectorTypeContext;
  class StructTypeContext;
  class NullableTypeContext;
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
    std::vector<antlr4::tree::TerminalNode *> SEP();
    antlr4::tree::TerminalNode* SEP(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  StmtListContext* stmtList();

  class  StmtContext : public antlr4::ParserRuleContext {
  public:
    StmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    LetStmtContext *letStmt();
    UseStmtContext *useStmt();
    FuncDefContext *funcDef();
    TypeDefContext *typeDef();
    EnumDefContext *enumDef();
    RetStmtContext *retStmt();
    ExprStmtContext *exprStmt();
    AssignStmtContext *assignStmt();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  StmtContext* stmt();

  class  LetStmtContext : public antlr4::ParserRuleContext {
  public:
    LetStmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *LET();
    CarrierContext *carrier();
    ExprContext *expr();
    TypeContext *type();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  LetStmtContext* letStmt();

  class  UseStmtContext : public antlr4::ParserRuleContext {
  public:
    UseStmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *USE();
    CarrierContext *carrier();
    ExprContext *expr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  UseStmtContext* useStmt();

  class  WithDefContext : public antlr4::ParserRuleContext {
  public:
    WithDefContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *WITH();
    EntityRefContext *entityRef();
    WithDeclContext *withDecl();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  WithDefContext* withDef();

  class  FuncDefContext : public antlr4::ParserRuleContext {
  public:
    FuncDefContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    AnnotationsContext *annotations();
    ModifiersContext *modifiers();
    antlr4::tree::TerminalNode *FUNC();
    IdentRefContext *identRef();
    ParamDefContext *paramDef();
    StmtPackContext *stmtPack();
    WithDefContext *withDef();
    TypeContext *type();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  FuncDefContext* funcDef();

  class  TypeDefContext : public antlr4::ParserRuleContext {
  public:
    TypeDefContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *TYPE();
    IdentRefContext *identRef();
    TypeContext *type();
    TypePackContext *typePack();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  TypeDefContext* typeDef();

  class  EnumDefContext : public antlr4::ParserRuleContext {
  public:
    EnumDefContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *ENUM();
    IdentRefContext *identRef();
    DictUnpackContext *dictUnpack();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  EnumDefContext* enumDef();

  class  RetStmtContext : public antlr4::ParserRuleContext {
  public:
    RetStmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *RETURN();
    ExprContext *expr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  RetStmtContext* retStmt();

  class  ExprStmtContext : public antlr4::ParserRuleContext {
  public:
    ExprStmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    AnnotationsContext *annotations();
    ExprContext *expr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ExprStmtContext* exprStmt();

  class  AssignStmtContext : public antlr4::ParserRuleContext {
  public:
    AssignStmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    EntityRefContext *entityRef();
    ExprContext *expr();
    TypeContext *type();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  AssignStmtContext* assignStmt();

  class  CarrierContext : public antlr4::ParserRuleContext {
  public:
    CarrierContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentRefContext *identRef();
    ListUnpackContext *listUnpack();
    DictUnpackContext *dictUnpack();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  CarrierContext* carrier();

  class  BiasAnnoContext : public antlr4::ParserRuleContext {
  public:
    BiasAnnoContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<ExprContext *> expr();
    ExprContext* expr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  BiasAnnoContext* biasAnno();

  class  SizeAnnoContext : public antlr4::ParserRuleContext {
  public:
    SizeAnnoContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<ExprContext *> expr();
    ExprContext* expr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  SizeAnnoContext* sizeAnno();

  class  AnnotationContext : public antlr4::ParserRuleContext {
  public:
    AnnotationContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentRefContext *identRef();
    DictPackContext *dictPack();
    BiasAnnoContext *biasAnno();
    SizeAnnoContext *sizeAnno();


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
    std::vector<antlr4::tree::TerminalNode *> SYNC();
    antlr4::tree::TerminalNode* SYNC(size_t i);
    std::vector<antlr4::tree::TerminalNode *> SCOPED();
    antlr4::tree::TerminalNode* SCOPED(size_t i);
    std::vector<antlr4::tree::TerminalNode *> STATIC();
    antlr4::tree::TerminalNode* STATIC(size_t i);
    std::vector<antlr4::tree::TerminalNode *> ATOMIC();
    antlr4::tree::TerminalNode* ATOMIC(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ModifiersContext* modifiers();

  class  WithListContext : public antlr4::ParserRuleContext {
  public:
    WithListContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<ArgumentContext *> argument();
    ArgumentContext* argument(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  WithListContext* withList();

  class  WithDeclContext : public antlr4::ParserRuleContext {
  public:
    WithDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<KeyValDeclContext *> keyValDecl();
    KeyValDeclContext* keyValDecl(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  WithDeclContext* withDecl();

  class  ParamDefContext : public antlr4::ParserRuleContext {
  public:
    ParamDefContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<KeyValDeclContext *> keyValDecl();
    KeyValDeclContext* keyValDecl(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ParamDefContext* paramDef();

  class  ArgsListContext : public antlr4::ParserRuleContext {
  public:
    ArgsListContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<ArgumentContext *> argument();
    ArgumentContext* argument(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ArgsListContext* argsList();

  class  ArgumentContext : public antlr4::ParserRuleContext {
  public:
    ArgumentContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentRefContext *identRef();
    EntityContext *entity();
    KeyValExprContext *keyValExpr();
    ExprContext *expr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ArgumentContext* argument();

  class  TypePackContext : public antlr4::ParserRuleContext {
  public:
    TypePackContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<KeyValDeclContext *> keyValDecl();
    KeyValDeclContext* keyValDecl(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  TypePackContext* typePack();

  class  KeyValDeclContext : public antlr4::ParserRuleContext {
  public:
    KeyValDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentRefContext *identRef();
    NullableTypeContext *nullableType();
    AnnotationContext *annotation();
    ExprContext *expr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  KeyValDeclContext* keyValDecl();

  class  KeyValExprContext : public antlr4::ParserRuleContext {
  public:
    KeyValExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentRefContext *identRef();
    ExprContext *expr();
    AnnotationContext *annotation();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  KeyValExprContext* keyValExpr();

  class  EntityRefContext : public antlr4::ParserRuleContext {
  public:
    EntityRefContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<IdentRefContext *> identRef();
    IdentRefContext* identRef(size_t i);
    AnnotationContext *annotation();
    std::vector<antlr4::tree::TerminalNode *> INTEGER();
    antlr4::tree::TerminalNode* INTEGER(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  EntityRefContext* entityRef();

  class  FunctorRefContext : public antlr4::ParserRuleContext {
  public:
    FunctorRefContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentRefContext *identRef();
    WithListContext *withList();
    AnnotationContext *annotation();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  FunctorRefContext* functorRef();

  class  ListUnpackContext : public antlr4::ParserRuleContext {
  public:
    ListUnpackContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<IdentRefContext *> identRef();
    IdentRefContext* identRef(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ListUnpackContext* listUnpack();

  class  DictUnpackContext : public antlr4::ParserRuleContext {
  public:
    DictUnpackContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<IdentRefContext *> identRef();
    IdentRefContext* identRef(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  DictUnpackContext* dictUnpack();

  class  DictPackContext : public antlr4::ParserRuleContext {
  public:
    DictPackContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<KeyValExprContext *> keyValExpr();
    KeyValExprContext* keyValExpr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  DictPackContext* dictPack();

  class  ListPackContext : public antlr4::ParserRuleContext {
  public:
    ListPackContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<ExprContext *> expr();
    ExprContext* expr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ListPackContext* listPack();

  class  StmtPackContext : public antlr4::ParserRuleContext {
  public:
    StmtPackContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    StmtListContext *stmtList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  StmtPackContext* stmtPack();

  class  LambdaContext : public antlr4::ParserRuleContext {
  public:
    LambdaContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    StmtPackContext *stmtPack();
    ParamDefContext *paramDef();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  LambdaContext* lambda();

  class  EntityExprContext : public antlr4::ParserRuleContext {
  public:
    EntityExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    EntityContext *entity();
    NormCallContext *normCall();
    LinkCallContext *linkCall();
    EntityChainContext *entityChain();
    antlr4::tree::TerminalNode *AS();
    TypeContext *type();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  EntityExprContext* entityExpr();

  class  EntityChainContext : public antlr4::ParserRuleContext {
  public:
    EntityChainContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<IdentRefContext *> identRef();
    IdentRefContext* identRef(size_t i);
    std::vector<EntityContext *> entity();
    EntityContext* entity(size_t i);
    std::vector<LinkCallContext *> linkCall();
    LinkCallContext* linkCall(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  EntityChainContext* entityChain();

  class  EntityContext : public antlr4::ParserRuleContext {
  public:
    EntityContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentRefContext *identRef();
    LiteralContext *literal();
    ListPackContext *listPack();
    DictPackContext *dictPack();
    LambdaContext *lambda();
    AnnotationContext *annotation();
    EntityRefContext *entityRef();
    FunctorRefContext *functorRef();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  EntityContext* entity();

  class  NormCallContext : public antlr4::ParserRuleContext {
  public:
    NormCallContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    FunctorRefContext *functorRef();
    ArgsListContext *argsList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  NormCallContext* normCall();

  class  LinkCallContext : public antlr4::ParserRuleContext {
  public:
    LinkCallContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<IdentRefContext *> identRef();
    IdentRefContext* identRef(size_t i);
    EntityRefContext *entityRef();
    std::vector<EntityContext *> entity();
    EntityContext* entity(size_t i);
    std::vector<FunctorRefContext *> functorRef();
    FunctorRefContext* functorRef(size_t i);
    std::vector<NormCallContext *> normCall();
    NormCallContext* normCall(size_t i);
    LinkCallContext *linkCall();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  LinkCallContext* linkCall();
  LinkCallContext* linkCall(int precedence);
  class  CalcExprContext : public antlr4::ParserRuleContext {
  public:
    CalcExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    RelaExprContext *relaExpr();
    CalcExprContext *calcExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  CalcExprContext* calcExpr();
  CalcExprContext* calcExpr(int precedence);
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
    LiteralContext *literal();
    EntityContext *entity();
    EntityExprContext *entityExpr();
    CalcExprContext *calcExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  PrimExprContext* primExpr();

  class  ExprContext : public antlr4::ParserRuleContext {
  public:
    ExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    EntityExprContext *entityExpr();
    CalcExprContext *calcExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ExprContext* expr();

  class  LiteralContext : public antlr4::ParserRuleContext {
  public:
    LiteralContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ValueContext *value();
    antlr4::tree::TerminalNode *STRING();
    antlr4::tree::TerminalNode *MULTI_STR();
    antlr4::tree::TerminalNode *FSTRING();
    antlr4::tree::TerminalNode *NULL_();
    antlr4::tree::TerminalNode *TRUE();
    antlr4::tree::TerminalNode *FALSE();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  LiteralContext* literal();

  class  ValueContext : public antlr4::ParserRuleContext {
  public:
    ValueContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *INTEGER();
    antlr4::tree::TerminalNode *REAL();
    antlr4::tree::TerminalNode *UNIT();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ValueContext* value();

  class  TypeContext : public antlr4::ParserRuleContext {
  public:
    TypeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    InnerTypeContext *innerType();
    IdentRefContext *identRef();
    antlr4::tree::TerminalNode *ANY_TYPE();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  TypeContext* type();

  class  InnerTypeContext : public antlr4::ParserRuleContext {
  public:
    InnerTypeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *NUMBER_TYPE();
    antlr4::tree::TerminalNode *STRING_TYPE();
    antlr4::tree::TerminalNode *BOOLEAN_TYPE();
    antlr4::tree::TerminalNode *FUNCTOR_TYPE();
    NumberTypeContext *numberType();
    StructTypeContext *structType();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  InnerTypeContext* innerType();

  class  NumberTypeContext : public antlr4::ParserRuleContext {
  public:
    NumberTypeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ScalarTypeContext *scalarType();
    VectorTypeContext *vectorType();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  NumberTypeContext* numberType();

  class  ScalarTypeContext : public antlr4::ParserRuleContext {
  public:
    ScalarTypeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *INTEGER_TYPE();
    antlr4::tree::TerminalNode *REAL_TYPE();
    antlr4::tree::TerminalNode *COMPLEX_TYPE();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ScalarTypeContext* scalarType();

  class  VectorTypeContext : public antlr4::ParserRuleContext {
  public:
    VectorTypeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *ARRAY_TYPE();
    ScalarTypeContext *scalarType();
    std::vector<antlr4::tree::TerminalNode *> INTEGER();
    antlr4::tree::TerminalNode* INTEGER(size_t i);
    antlr4::tree::TerminalNode *MATRIX_TYPE();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  VectorTypeContext* vectorType();

  class  StructTypeContext : public antlr4::ParserRuleContext {
  public:
    StructTypeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *LIST_TYPE();
    std::vector<NullableTypeContext *> nullableType();
    NullableTypeContext* nullableType(size_t i);
    antlr4::tree::TerminalNode *INTEGER();
    antlr4::tree::TerminalNode *DICT_TYPE();
    TypeContext *type();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  StructTypeContext* structType();

  class  NullableTypeContext : public antlr4::ParserRuleContext {
  public:
    NullableTypeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    TypeContext *type();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  NullableTypeContext* nullableType();

  class  IdentRefContext : public antlr4::ParserRuleContext {
  public:
    IdentRefContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *IDENTIFIER();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  IdentRefContext* identRef();


  bool sempred(antlr4::RuleContext *_localctx, size_t ruleIndex, size_t predicateIndex) override;

  bool linkCallSempred(LinkCallContext *_localctx, size_t predicateIndex);
  bool calcExprSempred(CalcExprContext *_localctx, size_t predicateIndex);
  bool relaExprSempred(RelaExprContext *_localctx, size_t predicateIndex);
  bool addExprSempred(AddExprContext *_localctx, size_t predicateIndex);
  bool multiExprSempred(MultiExprContext *_localctx, size_t predicateIndex);

  // By default the static state used to implement the parser is lazily initialized during the first
  // call to the constructor. You can call this function if you wish to initialize the static state
  // ahead of time.
  static void initialize();

private:
};

