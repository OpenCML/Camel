
// Generated from OpenCML.g4 by ANTLR 4.13.0

#pragma once


#include "antlr4-runtime.h"




class  OpenCMLParser : public antlr4::Parser {
public:
  enum {
    T__0 = 1, T__1 = 2, T__2 = 3, T__3 = 4, T__4 = 5, T__5 = 6, T__6 = 7, 
    T__7 = 8, T__8 = 9, T__9 = 10, T__10 = 11, T__11 = 12, T__12 = 13, T__13 = 14, 
    T__14 = 15, T__15 = 16, T__16 = 17, T__17 = 18, T__18 = 19, AS = 20, 
    LET = 21, USE = 22, FUNC = 23, TYPE = 24, ENUM = 25, WITH = 26, RETURN = 27, 
    INNER = 28, OUTER = 29, SYNC = 30, SCOPED = 31, STATIC = 32, ATOMIC = 33, 
    NULL_ = 34, TRUE = 35, FALSE = 36, ANY_TYPE = 37, NUMBER_TYPE = 38, 
    STRING_TYPE = 39, BOOLEAN_TYPE = 40, FUNCTOR_TYPE = 41, BLOCK_TYPE = 42, 
    INTEGER_TYPE = 43, REAL_TYPE = 44, COMPLEX_TYPE = 45, ARRAY_TYPE = 46, 
    MATRIX_TYPE = 47, LIST_TYPE = 48, DICT_TYPE = 49, SKIP_ = 50, LINE_END = 51, 
    MULTI_STR = 52, IDENTIFIER = 53, UNIT = 54, STRING = 55, FSTRING = 56, 
    INTEGER = 57, REAL = 58
  };

  enum {
    RuleProgram = 0, RuleStmtList = 1, RuleStmt = 2, RuleLetStmt = 3, RuleUseStmt = 4, 
    RuleWithDef = 5, RuleFuncDef = 6, RuleTypeDef = 7, RuleEnumDef = 8, 
    RuleRetStmt = 9, RuleExprStmt = 10, RuleCarrier = 11, RuleBiasAnno = 12, 
    RuleSizeAnno = 13, RuleAnnotation = 14, RuleAnnotations = 15, RuleModifiers = 16, 
    RuleWithList = 17, RuleWithDecl = 18, RuleParamDef = 19, RuleArgsList = 20, 
    RuleArgument = 21, RuleTypePack = 22, RuleKeyValDecl = 23, RuleKeyValExpr = 24, 
    RuleEntityRef = 25, RuleFunctorRef = 26, RuleListUnpack = 27, RuleDictUnpack = 28, 
    RuleDictPack = 29, RuleListPack = 30, RuleStmtPack = 31, RuleEntityExpr = 32, 
    RuleEntityChain = 33, RuleEntity = 34, RuleNormCall = 35, RuleLinkCall = 36, 
    RuleStmtEnd = 37, RuleSepMark = 38, RuleLiteral = 39, RuleValue = 40, 
    RuleType = 41, RuleInnerType = 42, RuleNumberType = 43, RuleScalarType = 44, 
    RuleComplex = 45, RuleVectorType = 46, RuleStructType = 47, RuleNullableType = 48, 
    RuleIdentRef = 49
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
  class EntityExprContext;
  class EntityChainContext;
  class EntityContext;
  class NormCallContext;
  class LinkCallContext;
  class StmtEndContext;
  class SepMarkContext;
  class LiteralContext;
  class ValueContext;
  class TypeContext;
  class InnerTypeContext;
  class NumberTypeContext;
  class ScalarTypeContext;
  class ComplexContext;
  class VectorTypeContext;
  class StructTypeContext;
  class NullableTypeContext;
  class IdentRefContext; 

  class  ProgramContext : public antlr4::ParserRuleContext {
  public:
    ProgramContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
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
    std::vector<SepMarkContext *> sepMark();
    SepMarkContext* sepMark(size_t i);


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
    StmtEndContext *stmtEnd();
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
    EntityExprContext *entityExpr();
    StmtEndContext *stmtEnd();


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
    antlr4::tree::TerminalNode *LINE_END();


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
    StmtEndContext *stmtEnd();
    WithDefContext *withDef();
    TypeContext *type();
    antlr4::tree::TerminalNode *LINE_END();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  FuncDefContext* funcDef();

  class  TypeDefContext : public antlr4::ParserRuleContext {
  public:
    TypeDefContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *TYPE();
    IdentRefContext *identRef();
    StmtEndContext *stmtEnd();
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
    StmtEndContext *stmtEnd();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  EnumDefContext* enumDef();

  class  RetStmtContext : public antlr4::ParserRuleContext {
  public:
    RetStmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *RETURN();
    StmtEndContext *stmtEnd();
    EntityExprContext *entityExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  RetStmtContext* retStmt();

  class  ExprStmtContext : public antlr4::ParserRuleContext {
  public:
    ExprStmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    AnnotationsContext *annotations();
    EntityExprContext *entityExpr();
    StmtEndContext *stmtEnd();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ExprStmtContext* exprStmt();

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
    std::vector<antlr4::tree::TerminalNode *> INTEGER();
    antlr4::tree::TerminalNode* INTEGER(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  BiasAnnoContext* biasAnno();

  class  SizeAnnoContext : public antlr4::ParserRuleContext {
  public:
    SizeAnnoContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<antlr4::tree::TerminalNode *> INTEGER();
    antlr4::tree::TerminalNode* INTEGER(size_t i);


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
    std::vector<antlr4::tree::TerminalNode *> LINE_END();
    antlr4::tree::TerminalNode* LINE_END(size_t i);


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
    std::vector<SepMarkContext *> sepMark();
    SepMarkContext* sepMark(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  WithListContext* withList();

  class  WithDeclContext : public antlr4::ParserRuleContext {
  public:
    WithDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<KeyValDeclContext *> keyValDecl();
    KeyValDeclContext* keyValDecl(size_t i);
    std::vector<SepMarkContext *> sepMark();
    SepMarkContext* sepMark(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  WithDeclContext* withDecl();

  class  ParamDefContext : public antlr4::ParserRuleContext {
  public:
    ParamDefContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<SepMarkContext *> sepMark();
    SepMarkContext* sepMark(size_t i);
    std::vector<KeyValDeclContext *> keyValDecl();
    KeyValDeclContext* keyValDecl(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ParamDefContext* paramDef();

  class  ArgsListContext : public antlr4::ParserRuleContext {
  public:
    ArgsListContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<SepMarkContext *> sepMark();
    SepMarkContext* sepMark(size_t i);
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
    EntityExprContext *entityExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ArgumentContext* argument();

  class  TypePackContext : public antlr4::ParserRuleContext {
  public:
    TypePackContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<SepMarkContext *> sepMark();
    SepMarkContext* sepMark(size_t i);
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
    EntityExprContext *entityExpr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  KeyValDeclContext* keyValDecl();

  class  KeyValExprContext : public antlr4::ParserRuleContext {
  public:
    KeyValExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentRefContext *identRef();
    EntityExprContext *entityExpr();


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
    std::vector<SepMarkContext *> sepMark();
    SepMarkContext* sepMark(size_t i);
    std::vector<IdentRefContext *> identRef();
    IdentRefContext* identRef(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ListUnpackContext* listUnpack();

  class  DictUnpackContext : public antlr4::ParserRuleContext {
  public:
    DictUnpackContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<SepMarkContext *> sepMark();
    SepMarkContext* sepMark(size_t i);
    std::vector<IdentRefContext *> identRef();
    IdentRefContext* identRef(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  DictUnpackContext* dictUnpack();

  class  DictPackContext : public antlr4::ParserRuleContext {
  public:
    DictPackContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<SepMarkContext *> sepMark();
    SepMarkContext* sepMark(size_t i);
    std::vector<KeyValExprContext *> keyValExpr();
    KeyValExprContext* keyValExpr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  DictPackContext* dictPack();

  class  ListPackContext : public antlr4::ParserRuleContext {
  public:
    ListPackContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<SepMarkContext *> sepMark();
    SepMarkContext* sepMark(size_t i);
    std::vector<EntityExprContext *> entityExpr();
    EntityExprContext* entityExpr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ListPackContext* listPack();

  class  StmtPackContext : public antlr4::ParserRuleContext {
  public:
    StmtPackContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    StmtListContext *stmtList();
    SepMarkContext *sepMark();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  StmtPackContext* stmtPack();

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
  class  StmtEndContext : public antlr4::ParserRuleContext {
  public:
    StmtEndContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    SepMarkContext *sepMark();
    antlr4::tree::TerminalNode *EOF();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  StmtEndContext* stmtEnd();

  class  SepMarkContext : public antlr4::ParserRuleContext {
  public:
    SepMarkContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<antlr4::tree::TerminalNode *> LINE_END();
    antlr4::tree::TerminalNode* LINE_END(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  SepMarkContext* sepMark();

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
    ComplexContext *complex();
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
    antlr4::tree::TerminalNode *BLOCK_TYPE();
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

  class  ComplexContext : public antlr4::ParserRuleContext {
  public:
    ComplexContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<antlr4::tree::TerminalNode *> INTEGER();
    antlr4::tree::TerminalNode* INTEGER(size_t i);
    std::vector<antlr4::tree::TerminalNode *> REAL();
    antlr4::tree::TerminalNode* REAL(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ComplexContext* complex();

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

  // By default the static state used to implement the parser is lazily initialized during the first
  // call to the constructor. You can call this function if you wish to initialize the static state
  // ahead of time.
  static void initialize();

private:
};

