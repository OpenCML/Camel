
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
    T__38 = 39, SEP = 40, AS = 41, LET = 42, USE = 43, FUNC = 44, TYPE = 45, 
    ENUM = 46, WITH = 47, RETURN = 48, INNER = 49, OUTER = 50, SYNC = 51, 
    SCOPED = 52, STATIC = 53, ATOMIC = 54, NULL_ = 55, TRUE = 56, FALSE = 57, 
    ANY_TYPE = 58, VOID_TYPE = 59, INTEGER_TYPE = 60, INTEGER32_TYPE = 61, 
    INTEGER64_TYPE = 62, REAL_TYPE = 63, FLOAT_TYPE = 64, DOUBLE_TYPE = 65, 
    NUMBER_TYPE = 66, STRING_TYPE = 67, BOOLEAN_TYPE = 68, FUNCTOR_TYPE = 69, 
    ARRAY_TYPE = 70, MATRIX_TYPE = 71, LIST_TYPE = 72, DICT_TYPE = 73, SKIP_ = 74, 
    MULTI_STR = 75, IDENTIFIER = 76, UNIT = 77, STRING = 78, FSTRING = 79, 
    INTEGER = 80, REAL = 81
  };

  enum {
    RuleProgram = 0, RuleStmtList = 1, RuleStmt = 2, RuleLetStmt = 3, RuleUseStmt = 4, 
    RuleExprStmt = 5, RuleAssignStmt = 6, RuleWithDef = 7, RuleFuncDef = 8, 
    RuleRetStmt = 9, RuleLambda = 10, RuleCarrier = 11, RuleAnnotation = 12, 
    RuleAnnotations = 13, RuleModifiers = 14, RuleArgument = 15, RuleArgsList = 16, 
    RuleExprList = 17, RuleKeyValExpr = 18, RuleKeyValList = 19, RuleDictPack = 20, 
    RuleListPack = 21, RuleWithDecl = 22, RuleWithPack = 23, RuleArgsDecl = 24, 
    RuleArgsPack = 25, RuleStmtPack = 26, RulePrimEntity = 27, RuleMemberAccess = 28, 
    RuleEntity = 29, RuleEntityLink = 30, RuleEntityChain = 31, RuleEntityUnpack = 32, 
    RuleEntityExpr = 33, RuleExpr = 34, RuleRelaExpr = 35, RuleAddExpr = 36, 
    RuleMultiExpr = 37, RuleUnaryExpr = 38, RulePrimExpr = 39, RuleLiteral = 40, 
    RuleValue = 41, RuleType = 42, RuleTypeUnit = 43, RuleInnerType = 44, 
    RuleScalarType = 45, RuleVectorType = 46, RuleStructType = 47, RuleIdentRef = 48
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
  class ArgumentContext;
  class ArgsListContext;
  class ExprListContext;
  class KeyValExprContext;
  class KeyValListContext;
  class DictPackContext;
  class ListPackContext;
  class WithDeclContext;
  class WithPackContext;
  class ArgsDeclContext;
  class ArgsPackContext;
  class StmtPackContext;
  class PrimEntityContext;
  class MemberAccessContext;
  class EntityContext;
  class EntityLinkContext;
  class EntityChainContext;
  class EntityUnpackContext;
  class EntityExprContext;
  class ExprContext;
  class RelaExprContext;
  class AddExprContext;
  class MultiExprContext;
  class UnaryExprContext;
  class PrimExprContext;
  class LiteralContext;
  class ValueContext;
  class TypeContext;
  class TypeUnitContext;
  class InnerTypeContext;
  class ScalarTypeContext;
  class VectorTypeContext;
  class StructTypeContext;
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
    FuncDefContext *funcDef();
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

  class  ExprStmtContext : public antlr4::ParserRuleContext {
  public:
    ExprStmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ExprContext *expr();
    AnnotationsContext *annotations();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ExprStmtContext* exprStmt();

  class  AssignStmtContext : public antlr4::ParserRuleContext {
  public:
    AssignStmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    EntityContext *entity();
    ExprContext *expr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  AssignStmtContext* assignStmt();

  class  WithDefContext : public antlr4::ParserRuleContext {
  public:
    WithDefContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *WITH();
    EntityContext *entity();
    WithDeclContext *withDecl();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  WithDefContext* withDef();

  class  FuncDefContext : public antlr4::ParserRuleContext {
  public:
    FuncDefContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *FUNC();
    IdentRefContext *identRef();
    ArgsDeclContext *argsDecl();
    StmtPackContext *stmtPack();
    AnnotationsContext *annotations();
    WithDefContext *withDef();
    ModifiersContext *modifiers();
    TypeContext *type();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  FuncDefContext* funcDef();

  class  RetStmtContext : public antlr4::ParserRuleContext {
  public:
    RetStmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *RETURN();
    ExprContext *expr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  RetStmtContext* retStmt();

  class  LambdaContext : public antlr4::ParserRuleContext {
  public:
    LambdaContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    StmtPackContext *stmtPack();
    ModifiersContext *modifiers();
    ArgsDeclContext *argsDecl();
    TypeContext *type();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  LambdaContext* lambda();

  class  CarrierContext : public antlr4::ParserRuleContext {
  public:
    CarrierContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentRefContext *identRef();
    ListPackContext *listPack();
    DictPackContext *dictPack();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  CarrierContext* carrier();

  class  AnnotationContext : public antlr4::ParserRuleContext {
  public:
    AnnotationContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    CarrierContext *carrier();


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

  class  ArgumentContext : public antlr4::ParserRuleContext {
  public:
    ArgumentContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ExprContext *expr();
    IdentRefContext *identRef();
    EntityUnpackContext *entityUnpack();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ArgumentContext* argument();

  class  ArgsListContext : public antlr4::ParserRuleContext {
  public:
    ArgsListContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<ArgumentContext *> argument();
    ArgumentContext* argument(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ArgsListContext* argsList();

  class  ExprListContext : public antlr4::ParserRuleContext {
  public:
    ExprListContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<ExprContext *> expr();
    ExprContext* expr(size_t i);
    std::vector<EntityUnpackContext *> entityUnpack();
    EntityUnpackContext* entityUnpack(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ExprListContext* exprList();

  class  KeyValExprContext : public antlr4::ParserRuleContext {
  public:
    KeyValExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentRefContext *identRef();
    AnnotationContext *annotation();
    TypeContext *type();
    ExprContext *expr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  KeyValExprContext* keyValExpr();

  class  KeyValListContext : public antlr4::ParserRuleContext {
  public:
    KeyValListContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<KeyValExprContext *> keyValExpr();
    KeyValExprContext* keyValExpr(size_t i);
    std::vector<EntityUnpackContext *> entityUnpack();
    EntityUnpackContext* entityUnpack(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  KeyValListContext* keyValList();

  class  DictPackContext : public antlr4::ParserRuleContext {
  public:
    DictPackContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    KeyValListContext *keyValList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  DictPackContext* dictPack();

  class  ListPackContext : public antlr4::ParserRuleContext {
  public:
    ListPackContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ExprListContext *exprList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ListPackContext* listPack();

  class  WithDeclContext : public antlr4::ParserRuleContext {
  public:
    WithDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    KeyValListContext *keyValList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  WithDeclContext* withDecl();

  class  WithPackContext : public antlr4::ParserRuleContext {
  public:
    WithPackContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ArgsListContext *argsList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  WithPackContext* withPack();

  class  ArgsDeclContext : public antlr4::ParserRuleContext {
  public:
    ArgsDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    KeyValListContext *keyValList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ArgsDeclContext* argsDecl();

  class  ArgsPackContext : public antlr4::ParserRuleContext {
  public:
    ArgsPackContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ArgsListContext *argsList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ArgsPackContext* argsPack();

  class  StmtPackContext : public antlr4::ParserRuleContext {
  public:
    StmtPackContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    StmtListContext *stmtList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  StmtPackContext* stmtPack();

  class  PrimEntityContext : public antlr4::ParserRuleContext {
  public:
    PrimEntityContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentRefContext *identRef();
    LiteralContext *literal();
    ListPackContext *listPack();
    DictPackContext *dictPack();
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
    std::vector<ExprContext *> expr();
    ExprContext* expr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  MemberAccessContext* memberAccess();

  class  EntityContext : public antlr4::ParserRuleContext {
  public:
    EntityContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    PrimEntityContext *primEntity();
    MemberAccessContext *memberAccess();
    WithPackContext *withPack();
    ArgsPackContext *argsPack();
    AnnotationContext *annotation();


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

  class  EntityUnpackContext : public antlr4::ParserRuleContext {
  public:
    EntityUnpackContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    EntityContext *entity();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  EntityUnpackContext* entityUnpack();

  class  EntityExprContext : public antlr4::ParserRuleContext {
  public:
    EntityExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    EntityChainContext *entityChain();
    antlr4::tree::TerminalNode *AS();
    TypeContext *type();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  EntityExprContext* entityExpr();

  class  ExprContext : public antlr4::ParserRuleContext {
  public:
    ExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    RelaExprContext *relaExpr();
    ExprContext *expr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ExprContext* expr();
  ExprContext* expr(int precedence);
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
    EntityExprContext *entityExpr();
    ExprContext *expr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  PrimExprContext* primExpr();

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
    TypeUnitContext *typeUnit();
    TypeContext *type();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  TypeContext* type();

  class  TypeUnitContext : public antlr4::ParserRuleContext {
  public:
    TypeUnitContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    InnerTypeContext *innerType();
    IdentRefContext *identRef();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  TypeUnitContext* typeUnit();

  class  InnerTypeContext : public antlr4::ParserRuleContext {
  public:
    InnerTypeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *ANY_TYPE();
    antlr4::tree::TerminalNode *VOID_TYPE();
    antlr4::tree::TerminalNode *STRING_TYPE();
    antlr4::tree::TerminalNode *BOOLEAN_TYPE();
    antlr4::tree::TerminalNode *FUNCTOR_TYPE();
    ScalarTypeContext *scalarType();
    VectorTypeContext *vectorType();
    StructTypeContext *structType();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  InnerTypeContext* innerType();

  class  ScalarTypeContext : public antlr4::ParserRuleContext {
  public:
    ScalarTypeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *INTEGER_TYPE();
    antlr4::tree::TerminalNode *INTEGER32_TYPE();
    antlr4::tree::TerminalNode *INTEGER64_TYPE();
    antlr4::tree::TerminalNode *REAL_TYPE();
    antlr4::tree::TerminalNode *FLOAT_TYPE();
    antlr4::tree::TerminalNode *DOUBLE_TYPE();
    antlr4::tree::TerminalNode *NUMBER_TYPE();


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
    std::vector<TypeContext *> type();
    TypeContext* type(size_t i);
    antlr4::tree::TerminalNode *INTEGER();
    antlr4::tree::TerminalNode *DICT_TYPE();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  StructTypeContext* structType();

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
  bool exprSempred(ExprContext *_localctx, size_t predicateIndex);
  bool relaExprSempred(RelaExprContext *_localctx, size_t predicateIndex);
  bool addExprSempred(AddExprContext *_localctx, size_t predicateIndex);
  bool multiExprSempred(MultiExprContext *_localctx, size_t predicateIndex);

  // By default the static state used to implement the parser is lazily initialized during the first
  // call to the constructor. You can call this function if you wish to initialize the static state
  // ahead of time.
  static void initialize();

private:
};

